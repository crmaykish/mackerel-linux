// SPDX-License-Identifier: GPL-2.0
// Mackerel-F board init and early console.

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <asm/machdep.h>
#include <asm/mackerel.h>
#include <asm/traps.h>

extern void legacy_timer_tick(unsigned long ticks);

static irqreturn_t mackerelf_tick(int irq, void *dummy)
{
	// Read the Timer STATUS register to clear the interrupt
	MEM(TIMER_STATUS) = 0;
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void mackerelf_sched_init(void)
{
	int ret;

	// Setup the timer interrupt
	ret = request_irq(IRQ_NUM_TIMER, mackerelf_tick, IRQF_TIMER, "timer", NULL);
	if (ret) {
		pr_err("Mackerel-F: cannot get timer IRQ: %d\n", ret);
		return;
	}

	// Start the timer
	MEM(TIMER_CTRL) = TIMER_ENABLE_100HZ;
}

static void mackerelf_console_write(struct console *co, const char *str,
				    unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count && str[i]; i++)
		uart16550_putc(str[i]);
}

static struct console mackerelf_console_driver = {
	.name  = "mackconsole",
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
	.write = mackerelf_console_write,
};

static void mackerelf_reset(void)
{
	local_irq_disable();
}

extern void mackerel_addr_err(void);
extern void mackerel_bus_err(void);

void __init config_BSP(char *command, int len)
{
	pr_info(MACKEREL_BOARD_NAME " support by Colin Maykish <crmaykish@protonmail.com>\n");

	// Early exception handlers
	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

	mach_reset      = mackerelf_reset;
	mach_sched_init = mackerelf_sched_init;

	register_console(&mackerelf_console_driver);
}
