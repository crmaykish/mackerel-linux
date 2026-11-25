// SPDX-License-Identifier: GPL-2.0
// Mackerel-10 interrupt controller setup

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/mackerel.h>
#include <asm/setup.h>

asmlinkage void system_call(void);
asmlinkage irqreturn_t bad_interrupt(int, void *);
asmlinkage irqreturn_t inthandler1(void);
asmlinkage irqreturn_t inthandler2(void);
asmlinkage irqreturn_t inthandler3(void);
asmlinkage irqreturn_t inthandler4(void);
asmlinkage irqreturn_t inthandler5(void);
asmlinkage irqreturn_t inthandler6(void);
asmlinkage irqreturn_t inthandler7(void);

void process_int(int vec, struct pt_regs *fp)
{
	int irq_num = 0;

	if (vec >= 25 && vec < 32) {
		/* Autovectored interrupts: autovector N = vector 24+N, irq N */
		irq_num = vec - 24;
	} else if (vec >= 65 && vec < 72) {
		/* User-vectored interrupts from DUART */
		if (vec == (64 + IRQ_NUM_DUART)) {
			/* Route all DUART sub-interrupts (TX and RX, both ports)
			 * to xr_isr, which reads ISR and demultiplexes them.
			 * The old MISR/RXRDY filter caused TX interrupts to be
			 * lost (irq_num stayed 0), silencing all tty TX output. */
			irq_num = IRQ_NUM_DUART;
		} else {
			irq_num = vec - 64;
		}
	} else {
		pr_warn("Unknown interrupt vector: %d\n", vec);
	}

	do_IRQ(irq_num, fp);
}

#ifdef CONFIG_MACKEREL10
// Mackerel-10 has a simple memory-mapped IRQ mask for the W5500 chip in the CPLD
static void intc_irq_unmask(struct irq_data *d) { MEM(INTC_BASE) |= (1 << d->irq); }
static void intc_irq_mask(struct irq_data *d)   { MEM(INTC_BASE) &= ~(1 << d->irq); }
#else
static void intc_irq_unmask(struct irq_data *d) {}
static void intc_irq_mask(struct irq_data *d) {}
#endif

static struct irq_chip intc_irq_chip = {
	.name      = "MACKEREL-INTC",
	.irq_mask  = intc_irq_mask,
	.irq_unmask = intc_irq_unmask,
};

void __init trap_init(void)
{
	int i;

	/* Fill all vectors first so no bootloader handlers remain active */
	for (i = 0; i < 256; i++)
		_ramvec[i] = (e_vector)bad_interrupt;

	/* CPU exceptions signal the faulting program */
	mackerel_trap_init();

	/* Autovectors (IPL 1-7) */
	_ramvec[25] = (e_vector)inthandler1;
	_ramvec[26] = (e_vector)inthandler2;
	_ramvec[27] = (e_vector)inthandler3;
	_ramvec[28] = (e_vector)inthandler4;
	_ramvec[29] = (e_vector)inthandler5;
	_ramvec[30] = (e_vector)inthandler6;
	_ramvec[31] = (e_vector)inthandler7;

	/* Syscall */
	_ramvec[32] = system_call;

	/* Vectored user interrupts (DUART at level 5, vector 0x45) */
	_ramvec[65] = (e_vector)inthandler1;
	_ramvec[66] = (e_vector)inthandler2;
	_ramvec[67] = (e_vector)inthandler3;
	_ramvec[68] = (e_vector)inthandler4;
	_ramvec[69] = (e_vector)inthandler5;
	_ramvec[70] = (e_vector)inthandler6;
	_ramvec[71] = (e_vector)inthandler7;
}

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_set_chip(i, &intc_irq_chip);
		irq_set_handler(i, handle_level_irq);
	}
}
