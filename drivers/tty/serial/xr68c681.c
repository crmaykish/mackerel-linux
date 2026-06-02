/*
 * Serial driver for the Exar XR68C681 DUART.
 *
 * Port B → ttyXR0 (system console), Port A → ttyXR1.
 * Baud rate, data bits, parity, and stop bits are programmable via set_termios.
 * Supports normal BRG rates up to 38400 and extended BRG rates up to 230400
 * (XR68C681-specific extended mode; uses CR commands 0x80/0xA0 to enable).
 *
 * Copyright (C) 2026 Colin Maykish <crmaykish@protonmail.com>
 * GPL v2.
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/serial_core.h>
#include <linux/serial.h>
#include <linux/tty.h>
#include <linux/tty_port.h>
#include <linux/tty_flip.h>
#include <linux/io.h>
#include <linux/interrupt.h>

#ifdef CONFIG_SERIAL_XR68C681_CONSOLE
#include <linux/console.h>
#endif

#include <asm/mackerel.h>

#define XR_NAME     "uart-xr68c681"
#define XR_NR_PORTS 2

/* ISR/IMR bit positions */
#define XR_ISR_TXRDY_A  BIT(0)
#define XR_ISR_RXRDY_A  BIT(1)
#define XR_ISR_TXRDY_B  BIT(4)
#define XR_ISR_RXRDY_B  BIT(5)

/* SR bit positions (same layout for both ports) */
#define XR_SR_RXRDY     BIT(0)
#define XR_SR_TXRDY     BIT(2)

/* CR command opcodes */
#define XR_CR_ENABLE_TXRX  0x05   /* bits [3:2]=01 (EN TX), [1:0]=01 (EN RX) */
#define XR_CR_DISABLE_TXRX 0x0A   /* bits [3:2]=10 (DIS TX), [1:0]=10 (DIS RX) */
#define XR_CR_RESET_MR     0x10   /* misc command: reset MR pointer to MR1 */

/* XR68C681 extended BRG mode commands (not in standard MC68681) */
#define XR_CR_SET_RX_EXT   0x80   /* misc command 8: enable extended BRG for RX */
#define XR_CR_CLR_RX_EXT   0x90   /* misc command 9: disable extended BRG for RX */
#define XR_CR_SET_TX_EXT   0xA0   /* misc command A: enable extended BRG for TX */
#define XR_CR_CLR_TX_EXT   0xB0   /* misc command B: disable extended BRG for TX */

/* MR1 data bits [1:0] */
#define XR_MR1_5BIT  0x00
#define XR_MR1_6BIT  0x01
#define XR_MR1_7BIT  0x02
#define XR_MR1_8BIT  0x03

/* MR1 parity: bits [4:3] = mode, bit [2] = type */
#define XR_MR1_NO_PARITY  0x10   /* mode = 10 (no parity) */
#define XR_MR1_ODD_PARITY 0x04   /* mode = 00, type = 1 (odd); even = 0x00 */

/* MR2 stop bits [3:0] (1 stop = 0111, 2 stop = 1111) */
#define XR_MR2_1STOP 0x07
#define XR_MR2_2STOP 0x0F

/* ACR bit 7: BRG set select (0 = Set 1, 1 = Set 2) */
#define XR_ACR_BRG_SET2 BIT(7)

/*
 * Baud rate table for 3.6864 MHz crystal.
 *
 * Normal BRG (MC68681 compatible, extended=false): rates up to 38400.
 * Extended BRG (XR68C681 only, extended=true): requires CR=0x80/0xA0 to
 * enable extended mode before programming CSR.
 *   Set 1 Extended: CSR=8→57600, CSR=9→115200, CSR=A→230400
 *   Set 2 Extended: CSR=8→115200 (confirmed), CSR=9→230400
 */
struct xr_baud {
	unsigned int baud;
	u8 acr7;      /* 1 = BRG Set 2 */
	u8 csr;       /* CSR nibble (written as (csr<<4)|csr for RX=TX) */
	bool extended; /* true = XR68C681 extended BRG mode required */
};

static const struct xr_baud xr_baud_table[] = {
	/* Normal BRG */
	{    50, 0, 0x0, false },
	{   110, 0, 0x1, false },
	{   200, 0, 0x3, false },
	{   300, 0, 0x4, false },
	{   600, 0, 0x5, false },
	{  1200, 0, 0x6, false },
	{  2400, 0, 0x8, false },
	{  4800, 0, 0x9, false },
	{  7200, 0, 0xA, false },
	{  9600, 0, 0xB, false },
	{ 19200, 1, 0xC, false },
	{ 38400, 0, 0xC, false },
	/* Extended BRG (XR68C681 only) */
	{  57600, 0, 0x8, true },   /* Set 1 extended */
	{ 115200, 1, 0x8, true },   /* Set 2 extended, confirmed by bootloader */
	{ 230400, 1, 0x9, true },   /* Set 2 extended */
};

struct xr_port {
	struct uart_port port;
	unsigned long reg_mr;    /* MR1/MR2 (auto-advance on write) */
	unsigned long reg_sr;    /* status register (read) */
	unsigned long reg_csr;   /* clock select register (write, same addr as SR) */
	unsigned long reg_cr;    /* command register */
	unsigned long reg_rb;    /* receive buffer (read) */
	unsigned long reg_tb;    /* transmit buffer (write, same addr as RB) */
	u8 tx_imr_bit;           /* IMR/ISR bit for this port's TX-ready */
	u8 rx_imr_bit;           /* IMR/ISR bit for this port's RX-ready */
};

static struct xr_port xr_ports[XR_NR_PORTS];

/* Shadows for write-only registers */
static u8 acr_shadow;
static u8 imr_shadow;

static inline u8 xr_readb(unsigned long reg)
{
	return *(volatile u8 *)reg;
}

static inline void xr_writeb(unsigned long reg, u8 val)
{
	*(volatile u8 *)reg = val;
}

/*
 * Program a port's mode, parity, stop bits, and baud rate.
 * Disables TX/RX around the register writes as required by the datasheet.
 * extended=true enables the XR68C681 extended BRG for rates above 38400.
 */
static void xr_port_program(struct xr_port *xp, u8 mr1, u8 mr2, u8 csr,
			    bool extended)
{
	xr_writeb(xp->reg_cr, XR_CR_DISABLE_TXRX);
	xr_writeb(xp->reg_cr, XR_CR_RESET_MR);
	xr_writeb(xp->reg_mr, mr1);
	xr_writeb(xp->reg_mr, mr2);   /* pointer auto-advances from MR1 to MR2 */
	if (extended) {
		xr_writeb(xp->reg_cr, XR_CR_SET_RX_EXT);
		xr_writeb(xp->reg_cr, XR_CR_SET_TX_EXT);
	} else {
		xr_writeb(xp->reg_cr, XR_CR_CLR_RX_EXT);
		xr_writeb(xp->reg_cr, XR_CR_CLR_TX_EXT);
	}
	xr_writeb(xp->reg_csr, (csr << 4) | csr);  /* same rate for RX and TX */
	xr_writeb(xp->reg_cr, XR_CR_ENABLE_TXRX);
}

/* === UART ops === */

static unsigned int xr_tx_empty(struct uart_port *port)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);
	return (xr_readb(xp->reg_sr) & XR_SR_TXRDY) ? TIOCSER_TEMT : 0;
}

static void xr_start_tx(struct uart_port *port)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);

	if (uart_tx_stopped(port))
		return;
	/* Enable TXRDY interrupt — the ISR drains the fifo one char at a time. */
	imr_shadow |= xp->tx_imr_bit;
	xr_writeb(DUART1_IMR, imr_shadow);
}

static void xr_stop_tx(struct uart_port *port)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);
	imr_shadow &= ~xp->tx_imr_bit;
	xr_writeb(DUART1_IMR, imr_shadow);
}
static void xr_stop_rx(struct uart_port *port) { }
static void xr_break_ctl(struct uart_port *port, int break_state) { }
static void xr_set_mctrl(struct uart_port *port, unsigned int mctrl) { }

static unsigned int xr_get_mctrl(struct uart_port *port)
{
	return TIOCM_CTS | TIOCM_DSR | TIOCM_CAR;
}

static void xr_set_termios(struct uart_port *port, struct ktermios *termios,
			   const struct ktermios *old)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);
	unsigned int baud;
	u8 mr1, mr2, csr = 0xB;  /* default: 9600 baud, BRG Set 1 */
	bool extended = false;
	int i;

	switch (termios->c_cflag & CSIZE) {
	case CS5: mr1 = XR_MR1_5BIT; break;
	case CS6: mr1 = XR_MR1_6BIT; break;
	case CS7: mr1 = XR_MR1_7BIT; break;
	default:  mr1 = XR_MR1_8BIT; break;
	}

	if (termios->c_cflag & PARENB) {
		if (termios->c_cflag & PARODD)
			mr1 |= XR_MR1_ODD_PARITY;
		/* even parity: mode=00 type=0, bits already clear */
	} else {
		mr1 |= XR_MR1_NO_PARITY;
	}

	mr2 = (termios->c_cflag & CSTOPB) ? XR_MR2_2STOP : XR_MR2_1STOP;

	baud = uart_get_baud_rate(port, termios, old, 50, 230400);

	for (i = ARRAY_SIZE(xr_baud_table) - 1; i >= 0; i--) {
		if (baud >= xr_baud_table[i].baud) {
			csr = xr_baud_table[i].csr;
			baud = xr_baud_table[i].baud;
			extended = xr_baud_table[i].extended;
			if (xr_baud_table[i].acr7)
				acr_shadow |= XR_ACR_BRG_SET2;
			else
				acr_shadow &= ~XR_ACR_BRG_SET2;
			xr_writeb(DUART1_ACR, acr_shadow);
			break;
		}
	}

	uart_update_timeout(port, termios->c_cflag, baud);
	xr_port_program(xp, mr1, mr2, csr, extended);
}

static int xr_startup(struct uart_port *port)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);

	imr_shadow |= xp->rx_imr_bit;
	xr_writeb(DUART1_IMR, imr_shadow);
	return 0;
}

static void xr_shutdown(struct uart_port *port)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);

	imr_shadow &= ~xp->rx_imr_bit;
	xr_writeb(DUART1_IMR, imr_shadow);
}

static const char *xr_type(struct uart_port *port)
{
	return XR_NAME;
}

static void xr_release_port(struct uart_port *port) { }
static int xr_request_port(struct uart_port *port) { return 0; }
static void xr_config_port(struct uart_port *port, int flags) { port->type = PORT_16550A; }
static int xr_verify_port(struct uart_port *port, struct serial_struct *ser) { return 0; }

static const struct uart_ops xr_ops = {
	.tx_empty     = xr_tx_empty,
	.set_mctrl    = xr_set_mctrl,
	.get_mctrl    = xr_get_mctrl,
	.stop_tx      = xr_stop_tx,
	.start_tx     = xr_start_tx,
	.stop_rx      = xr_stop_rx,
	.break_ctl    = xr_break_ctl,
	.startup      = xr_startup,
	.shutdown     = xr_shutdown,
	.set_termios  = xr_set_termios,
	.type         = xr_type,
	.release_port = xr_release_port,
	.request_port = xr_request_port,
	.config_port  = xr_config_port,
	.verify_port  = xr_verify_port,
};

/* === RX interrupt handler === */

static irqreturn_t xr_isr(int irq, void *dev_id)
{
	unsigned char isr = xr_readb(DUART1_ISR);
	irqreturn_t ret = IRQ_NONE;
	int i;

	for (i = 0; i < XR_NR_PORTS; i++) {
		struct xr_port *xp = &xr_ports[i];
		struct uart_port *port = &xp->port;

		if (!port->state)
			continue;

		/* TX: fill FIFO while TXRDY and IMR enabled */
		if ((isr & xp->tx_imr_bit) && (imr_shadow & xp->tx_imr_bit)) {
			while (xr_readb(xp->reg_sr) & XR_SR_TXRDY) {
				unsigned char ch;
				if (!uart_fifo_get(port, &ch)) {
					/* Fifo drained: disable TX interrupt */
					imr_shadow &= ~xp->tx_imr_bit;
					xr_writeb(DUART1_IMR, imr_shadow);
					uart_write_wakeup(port);
					break;
				}
				xr_writeb(xp->reg_tb, ch);
			}
			ret = IRQ_HANDLED;
		}

		/* RX: drain FIFO while RXRDY and IMR enabled */
		if ((isr & xp->rx_imr_bit) && (imr_shadow & xp->rx_imr_bit)) {
			while (xr_readb(xp->reg_sr) & XR_SR_RXRDY) {
				unsigned char c = xr_readb(xp->reg_rb);
				uart_insert_char(port, 0, 0, c, TTY_NORMAL);
			}
			tty_flip_buffer_push(&port->state->port);
			ret = IRQ_HANDLED;
		}
	}

	return ret;
}

/* === Console support === */

#ifdef CONFIG_SERIAL_XR68C681_CONSOLE

static void xr_console_putchar(struct uart_port *port, unsigned char c)
{
	struct xr_port *xp = container_of(port, struct xr_port, port);

	while (!(xr_readb(xp->reg_sr) & XR_SR_TXRDY))
		cpu_relax();
	xr_writeb(xp->reg_tb, c);
}

static void xr_console_write(struct console *co, const char *s, unsigned int count)
{
	uart_console_write(&xr_ports[0].port, s, count, xr_console_putchar);
}

static int xr_console_setup(struct console *co, char *options)
{
	struct uart_port *port;
	int baud = 115200, parity = 'n', bits = 8, flow = 'n';

	if (co->index < 0 || co->index >= XR_NR_PORTS)
		co->index = 0;
	port = &xr_ports[co->index].port;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(port, co, baud, parity, bits, flow);
}

extern struct console xr_console;

struct uart_driver xr_uart_driver = {
	.owner       = THIS_MODULE,
	.driver_name = XR_NAME,
	.dev_name    = "ttyXR",
	.major       = 0,
	.minor       = 0,
	.nr          = XR_NR_PORTS,
	.cons        = &xr_console,
};

struct console xr_console = {
	.name   = "ttyXR",
	.write  = xr_console_write,
	.device = uart_console_device,
	.setup  = xr_console_setup,
	.flags  = CON_PRINTBUFFER,
	.index  = 0,
	.data   = &xr_uart_driver,
};

#else

struct uart_driver xr_uart_driver = {
	.owner       = THIS_MODULE,
	.driver_name = XR_NAME,
	.dev_name    = "ttyXR",
	.major       = 0,
	.minor       = 0,
	.nr          = XR_NR_PORTS,
};

#endif /* CONFIG_SERIAL_XR68C681_CONSOLE */

/*
 * Register layout (all offsets from DUART1_BASE, byte-wide access):
 *   Port B: MR=+0x11, SR/CSR=+0x13, CR=+0x15, RB/TB=+0x17  → ttyXR0 (console)
 *   Port A: MR=+0x01, SR/CSR=+0x03, CR=+0x05, RB/TB=+0x07  → ttyXR1
 */
static void xr_init_port(struct xr_port *xp, int index, unsigned long base)
{
	if (index == 0) {
		/* Port B: ttyXR0 */
		xp->reg_mr  = base + 0x11;
		xp->reg_sr  = base + 0x13;
		xp->reg_csr = base + 0x13;
		xp->reg_cr  = base + 0x15;
		xp->reg_rb  = base + 0x17;
		xp->reg_tb  = base + 0x17;
		xp->tx_imr_bit = XR_ISR_TXRDY_B;
		xp->rx_imr_bit = XR_ISR_RXRDY_B;
	} else {
		/* Port A: ttyXR1 */
		xp->reg_mr  = base + 0x01;
		xp->reg_sr  = base + 0x03;
		xp->reg_csr = base + 0x03;
		xp->reg_cr  = base + 0x05;
		xp->reg_rb  = base + 0x07;
		xp->reg_tb  = base + 0x07;
		xp->tx_imr_bit = XR_ISR_TXRDY_A;
		xp->rx_imr_bit = XR_ISR_RXRDY_A;
	}
}

static int xr_probe(struct platform_device *pdev)
{
	int irq, ret, i;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no IRQ\n");
		return irq;
	}

	/* Mask all DUART interrupts and drain both RX FIFOs. */
	imr_shadow = 0x00;
	acr_shadow = 0x00;
	xr_writeb(DUART1_IMR, imr_shadow);
	while (xr_readb(DUART1_SRA) & XR_SR_RXRDY)
		(void)xr_readb(DUART1_RBA);
	while (xr_readb(DUART1_SRB) & XR_SR_RXRDY)
		(void)xr_readb(DUART1_RBB);

	/*
	 * Program IVR so the DUART drives vector 0x45 on IACK.
	 * VEC_USER (0x40) + IRQ level 5 = 0x45.
	 */
	xr_writeb(DUART1_IVR, 0x45);

	/* Register IRQ handler before uart_add_one_port so it is live when
	 * xr_startup enables interrupts during console port initialization. */
	ret = request_irq(irq, xr_isr, 0, XR_NAME, xr_ports);
	if (ret) {
		dev_err(&pdev->dev, "failed to request IRQ %d\n", irq);
		return ret;
	}

	for (i = 0; i < XR_NR_PORTS; i++) {
		struct xr_port *xp = &xr_ports[i];
		struct uart_port *port = &xp->port;

		xr_init_port(xp, i, DUART1_BASE);

		port->irq      = irq;
		port->iotype   = UPIO_MEM;
		port->mapbase  = DUART1_BASE;
		port->membase  = (void __iomem *)DUART1_BASE;
		port->ops      = &xr_ops;
		port->flags    = UPF_BOOT_AUTOCONF | UPF_FIXED_PORT;
		port->line     = i;
		port->uartclk  = 3686400;
		port->fifosize = 3;
		port->dev      = &pdev->dev;
		port->type     = PORT_16550A;

		ret = uart_add_one_port(&xr_uart_driver, port);
		if (ret) {
			dev_err(&pdev->dev, "uart_add_one_port(%d) failed: %d\n", i, ret);
			free_irq(irq, xr_ports);
			while (--i >= 0)
				uart_remove_one_port(&xr_uart_driver,
						     &xr_ports[i].port);
			return ret;
		}
	}

	/* Enable Port B RX interrupt for console input; Port A enabled on open. */
	imr_shadow = XR_ISR_RXRDY_B;
	xr_writeb(DUART1_IMR, imr_shadow);

	dev_info(&pdev->dev, "XR68C681 at 0x%llx IRQ %d\n",
		 (unsigned long long)DUART1_BASE, irq);
	return 0;
}

static void xr_remove(struct platform_device *pdev)
{
	int i;

	xr_writeb(DUART1_IMR, 0x00);
	free_irq(xr_ports[0].port.irq, xr_ports);
	for (i = 0; i < XR_NR_PORTS; i++)
		uart_remove_one_port(&xr_uart_driver, &xr_ports[i].port);
}

static struct platform_driver xr_plat_driver = {
	.driver = {
		.name = XR_NAME,
	},
	.probe  = xr_probe,
	.remove = xr_remove,
};

static int __init xr_init(void)
{
	int ret;

	ret = uart_register_driver(&xr_uart_driver);
	if (ret) {
		pr_err("%s: failed to register UART driver\n", XR_NAME);
		return ret;
	}

	/*
	 * Default a fresh open of /dev/ttyXR* to 115200 8N1 with CLOCAL set, so
	 * (a) opening the port doesn't block in tty_port_block_til_ready waiting
	 * for carrier, and (b) the baud matches the console (a plain open would
	 * otherwise inherit B38400 and garble the link). This is what a userspace
	 * "exec sh </dev/ttyXR0" relies on.
	 */
	xr_uart_driver.tty_driver->init_termios.c_cflag =
		B115200 | CS8 | CREAD | CLOCAL | HUPCL;
	xr_uart_driver.tty_driver->init_termios.c_ispeed = 115200;
	xr_uart_driver.tty_driver->init_termios.c_ospeed = 115200;

	ret = platform_driver_register(&xr_plat_driver);
	if (ret) {
		pr_err("%s: failed to register platform driver\n", XR_NAME);
		uart_unregister_driver(&xr_uart_driver);
	}

	return ret;
}

static void __exit xr_exit(void)
{
	platform_driver_unregister(&xr_plat_driver);
	uart_unregister_driver(&xr_uart_driver);
}

module_init(xr_init);
module_exit(xr_exit);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("XR68C681 DUART serial driver");
MODULE_AUTHOR("Colin Maykish <crmaykish@protonmail.com>");
