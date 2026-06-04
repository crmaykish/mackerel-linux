// SPDX-License-Identifier: GPL-2.0
// Mackerel-08/10 board init, timer, and early console

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <asm/machdep.h>
#include <asm/mackerel.h>
#include <asm/traps.h>
#include <asm/irq.h>
#include <linux/platform_data/pata_mackerel.h>

extern void legacy_timer_tick(unsigned long ticks);

#ifdef CONFIG_MACKEREL08
// Mackerel-08 has no timer chip; the XR68C681 counter/timer is the system tick.
// It shares the DUART IRQ with the serial port, so the handler is shared and
// ticks only when the counter-ready bit is set.
#define DUART_XTAL_HZ	3686400UL
#define TIMER_CLK_HZ	(DUART_XTAL_HZ / 16)	/* ACR[6:4]=111: X1/CLK / 16 */

// Mackerel-08 DUART interrupts fire once per cycle, so the preset is for half the desired tick rate
// Timer is configured for 50 Hz
// Note: the poor 68008 cannot keep up with higher interrupt rates
#define TIMER_PRESET	(TIMER_CLK_HZ / (2 * HZ))

static int mackerel_timer_id;

static irqreturn_t mackerel_timer_isr(int irq, void *dev_id)
{
	if (!(MEM(DUART1_ISR) & DUART_INTR_COUNTER))
	{
		return IRQ_NONE;
	}

	MEM(DUART1_OPR_RESET);
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void mackerel_sched_init(void)
{
	int ret;

	// Set up DUART timer mode
	MEM(DUART1_ACR) = DUART_ACR_RESERVED;
	MEM(DUART1_CUR) = (TIMER_PRESET >> 8) & 0xFF;
	MEM(DUART1_CLR) = TIMER_PRESET & 0xFF;
	MEM(DUART1_IMR) = DUART_INTR_COUNTER;
	// Start the timer
	MEM(DUART1_OPR);

	ret = request_irq(IRQ_NUM_DUART, mackerel_timer_isr, IRQF_SHARED,
			  "mackerel-timer", &mackerel_timer_id);
	if (ret) {
		pr_err("Mackerel-08: cannot get timer IRQ: %d\n", ret);
	}
}
#else // Mackerel-10
static irqreturn_t hw_tick(int irq, void *dummy)
{
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void mackerel_sched_init(void)
{
	int ret;

	ret = request_irq(IRQ_NUM_TIMER, hw_tick, IRQF_TIMER, "timer", NULL);
	if (ret)
		pr_err("Mackerel-10: cannot get timer IRQ: %d\n", ret);
}
#endif

static void mackerel_console_write(struct console *co, const char *str,
				   unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count && str[i]; i++)
		duart_putc(str[i]);
}

static struct console mackerel_console_driver = {
	.name  = "mackconsole",
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
	.write = mackerel_console_write,
};

static void mackerel_reset(void)
{
	local_irq_disable();
}

static struct resource uart_res[] = {
	{
		.start = DUART1_BASE,
		.end   = DUART1_BASE + 31,
		.flags = IORESOURCE_MEM,
	},
	{
		.start = IRQ_NUM_DUART,
		.end   = IRQ_NUM_DUART,
		.flags = IORESOURCE_IRQ,
	},
};

static struct platform_device xr68c681_device = {
	.name         = "uart-xr68c681",
	.id           = 0,
	.num_resources = ARRAY_SIZE(uart_res),
	.resource     = uart_res,
};

#ifdef CONFIG_MACKEREL10
static struct resource ide_res[] = {
	{
		/* Command block (CS0): 8 regs at 2-byte stride */
		.start = IDE_BASE,
		.end   = IDE_BASE + 0x0F,
		.flags = IORESOURCE_MEM,
	},
	{
		/* Control block (CS1): alt status / device control register */
		.start = IDE_CTL_BASE,
		.end   = IDE_CTL_BASE + 0x01,
		.flags = IORESOURCE_MEM,
	},
	{
		/* Autovector level 3 — CPLD raises IPL=3 on drive INTRQ */
		.start = IRQ_NUM_IDE,
		.end   = IRQ_NUM_IDE,
		.flags = IORESOURCE_IRQ,
	},
};

static struct pata_mackerel_pdata ide_pdata = {
	// No databus bit reversal like Mackerel-30
	.low_byte_bitrev = false,
};

static struct platform_device ide_device = {
	.name          = "pata-mackerel",
	.id            = -1,
	.num_resources = ARRAY_SIZE(ide_res),
	.resource      = ide_res,
	.dev = {
		.platform_data = &ide_pdata,
	},
};
#endif

extern void mackerel_addr_err(void);
extern void mackerel_bus_err(void);

void __init config_BSP(char *command, int len)
{
	pr_info(MACKEREL_BOARD_NAME " support by Colin Maykish <crmaykish@protonmail.com>\n");

	// Set up some early exception handlers
	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

	// Disable DUART interrupts, let the driver set them up later
	MEM(DUART1_IMR) = 0;
	MEM(DUART1_IVR) = 0x40 + IRQ_NUM_DUART;

	mach_reset     = mackerel_reset;
	mach_sched_init = mackerel_sched_init;

	register_console(&mackerel_console_driver);
}

static int __init mackerel_platform_init(void)
{
	if (platform_device_register(&xr68c681_device))
		panic("Could not register DUART device");
#ifdef CONFIG_MACKEREL10
	if (platform_device_register(&ide_device))
		pr_err("Mackerel-10: could not register IDE device\n");
#endif
	return 0;
}
arch_initcall(mackerel_platform_init);
