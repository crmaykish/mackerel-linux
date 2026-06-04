#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/platform_device.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <asm/machdep.h>
#include <asm/mackerel.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/bootinfo.h>
#include <linux/timer.h>
#include <linux/sched.h>
#include <linux/ioport.h>
#include <linux/serial_core.h>
#include <linux/platform_data/serial-xr68c681.h>
#include <linux/platform_data/pata_mackerel.h>

static void mackerel_console_write(struct console *co, const char *s,
				   unsigned int count)
{
	unsigned int i;
	for (i = 0; i < count; i++) {
		if (s[i] == '\n')
			duart_putc('\r');
		duart_putc(s[i]);
	}
}

static struct console mackerel_early_console = {
	.name = "mackerel",
	.write = mackerel_console_write,
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
};

static struct xr68c681_pdata xr_pdata = {
	.nr_ports = 2,
	.reg_shift = 1,
	.uartclk = 3686400,
};

/* TODO: replace base/size with actual mapped DUART address range */
static struct resource xr_res[] = {
	{
		.start = DUART1_BASE,
		.end   = DUART1_BASE + 0x20 - 1,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_USER + 5,
		.end   = IRQ_USER + 5,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device xr_dev = {
	.name = "uart-xr68c681",
	.id = -1,
	.num_resources = ARRAY_SIZE(xr_res),
	.resource = xr_res,
	.dev = {
		.platform_data = &xr_pdata,
	},
};

/* IDE: two 74HC245 buffers, low byte bit-reversed (see pata_mackerel.c) */
static struct resource ide_res[] = {
	{
		/* Command block registers: CS0, 8 regs at 2-byte stride = 16 bytes */
		.start = IDE_BASE,
		.end   = IDE_BASE + 0x0F,
		.flags = IORESOURCE_MEM,
	},
	{
		/* Control block register: CS1 */
		.start = IDE_CTL_BASE,
		.end   = IDE_CTL_BASE + 0x0F,
		.flags = IORESOURCE_MEM,
	},
	{
		/* Autovector level 3 — CPLD raises IPL=3 when drive asserts INTRQ */
		.start = IRQ_AUTO_3,
		.end   = IRQ_AUTO_3,
		.flags = IORESOURCE_IRQ,
	},
};

static struct pata_mackerel_pdata ide_pdata = {
	// Low byte is bit-reversed due to hardware bug
	.low_byte_bitrev = true,
};

static struct platform_device ide_dev = {
	.name          = "pata-mackerel",
	.id            = -1,
	.num_resources = ARRAY_SIZE(ide_res),
	.resource      = ide_res,
	.dev = {
		.platform_data = &ide_pdata,
	},
};

/* SPI: bitbang through DUART OP/IP pins; share DUART base with xr68c681 */
static struct resource spi_res[] = {
	{
		.start = DUART1_BASE,
		.end   = DUART1_BASE + 0x20 - 1,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device spi_dev = {
	.name          = "spi-mackerel",
	.id            = 0,
	.num_resources = ARRAY_SIZE(spi_res),
	.resource      = spi_res,
};


static int __init mackerel_early_console_init(void)
{
	register_console(&mackerel_early_console);
	return 0;
}
console_initcall(mackerel_early_console_init);

static int __init mackerel_platform_init(void)
{
	printk(KERN_INFO "Mackerel-30: Registering platform devices\n");

	if (platform_device_register(&xr_dev))
		printk(KERN_ERR "Failed to register XR68C681 UART device\n");
	else
		printk(KERN_INFO "Registered XR68C681 UART device\n");

	if (platform_device_register(&ide_dev))
		printk(KERN_ERR "Failed to register Mackerel IDE device\n");
	else
		printk(KERN_INFO "Registered Mackerel IDE device\n");

	if (platform_device_register(&spi_dev))
		printk(KERN_ERR "Failed to register Mackerel SPI device\n");
	else
		printk(KERN_INFO "Registered Mackerel SPI device\n");

	return 0;
}
arch_initcall(mackerel_platform_init);

int __init mackerel_parse_bootinfo(const struct bi_record *record)
{
	return 0;
}

static irqreturn_t mackerel_timer_isr(int irq, void *dev_id)
{
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void __init mackerel_sched_init(void)
{
	printk(KERN_INFO "Mackerel-30: Setting up system timer\n");

	if (request_irq(IRQ_AUTO_6, mackerel_timer_isr, IRQF_TIMER, "cpld-timer", NULL)) {
		pr_err("Failed to request CPLD timer logical IRQ %d (raw vec 0x%02x)\n", IRQ_AUTO_6, 0x40 + IRQ_AUTO_6);
	}
	else {
		printk(KERN_INFO "CPLD timer logical IRQ %d mapped to raw vector 0x%02x\n", IRQ_AUTO_6, 0x40 + IRQ_AUTO_6);
	}

	// Enable the timer interrupt on the CPLD
	MEM(TIMER_ENABLE) = 0xFF;
}

void __init mackerel_init_IRQ(void)
{
	printk(KERN_INFO "Mackerel-30: Init IRQ\n");

	/* Enable a block of user vectored interrupts starting at VEC_USER.
	 * We need at least 5 so that IRQ_USER + 4 is valid; reserve 16. */
	m68k_setup_user_interrupt(VEC_USER, 16);
}

void __init config_mackerel(void)
{
	printk(KERN_INFO "Mackerel-30 support by Colin Maykish <crmaykish@protonmail.com>\n");

	mach_sched_init = mackerel_sched_init;
	mach_init_IRQ = mackerel_init_IRQ;

	return;
}
