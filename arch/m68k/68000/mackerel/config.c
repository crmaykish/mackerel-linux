// SPDX-License-Identifier: GPL-2.0
// Mackerel-10 board init and early console

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

extern void legacy_timer_tick(unsigned long ticks);

static unsigned long tick_count;

static irqreturn_t hw_tick(int irq, void *dummy)
{
	legacy_timer_tick(1);
	tick_count++;
	return IRQ_HANDLED;
}

static void mackerel_sched_init(void)
{
	int ret;

	pr_info("Mackerel-10: timer on IRQ%d (PLD, autovectored)\n",
		IRQ_NUM_TIMER);

	ret = request_irq(IRQ_NUM_TIMER, hw_tick, IRQF_TIMER, "timer", NULL);
	if (ret)
		pr_err("Mackerel-10: failed to request timer IRQ: %d\n", ret);
}

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

extern void mackerel_addr_err(void);
extern void mackerel_bus_err(void);

void __init config_BSP(char *command, int len)
{
	pr_info("Mackerel-10 support by Colin Maykish <crmaykish@protonmail.com>\n");

	/*
	 * Install bus/address error handlers immediately so any fault during
	 * the rest of setup_arch() goes through the kernel panic path rather
	 * than the bootloader's handler.  trap_init() will overwrite these
	 * again later along with the full vector table.
	 */
	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

	/* Disable all DUART interrupts initially; serial driver will re-enable */
	MEM(DUART1_IMR) = 0;
	/* Set DUART interrupt vector so IACK cycles return the right vector */
	MEM(DUART1_IVR) = 0x40 + IRQ_NUM_DUART;

	mach_reset     = mackerel_reset;
	mach_sched_init = mackerel_sched_init;

	register_console(&mackerel_console_driver);
}

static int __init mackerel_platform_init(void)
{
	if (platform_device_register(&xr68c681_device))
		panic("Could not register DUART device");
	return 0;
}
arch_initcall(mackerel_platform_init);
