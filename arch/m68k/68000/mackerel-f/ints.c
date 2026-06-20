// SPDX-License-Identifier: GPL-2.0
// Mackerel-F interrupt controller setup.
// All interrupt sources on Mackerel-F are autovectored

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
	if (vec >= 65 && vec < 72)
		irq_num = vec - 64;
	else
		pr_warn("Unknown interrupt vector: %d\n", vec);

	do_IRQ(irq_num, fp);
}

// Per-level enable bits in the SoC interrupt controller
static void intc_irq_unmask(struct irq_data *d) { MEM(INTC_BASE) |= (1 << d->irq); }
static void intc_irq_mask(struct irq_data *d) { MEM(INTC_BASE) &= ~(1 << d->irq); }

static struct irq_chip intc_irq_chip = {
	.name       = "MACKEREL-INTC",
	.irq_mask   = intc_irq_mask,
	.irq_unmask = intc_irq_unmask,
};

// Set up some early exception handlers to ease debugging
asm(
	".globl mackerel_addr_err\n"
	"mackerel_addr_err:\n"
	"	move.l	2(%sp), %d0\n"
	"	move.l	10(%sp), %d1\n"
	"	move.l	%d1, -(%sp)\n"
	"	move.l	%d0, -(%sp)\n"
	"	jsr	mackerel_addr_err_c\n"
	"0:	bra.s	0b\n"
);

asm(
	".globl mackerel_bus_err\n"
	"mackerel_bus_err:\n"
	"	move.l	2(%sp), %d0\n"
	"	move.l	10(%sp), %d1\n"
	"	move.l	%d1, -(%sp)\n"
	"	move.l	%d0, -(%sp)\n"
	"	jsr	mackerel_bus_err_c\n"
	"0:	bra.s	0b\n"
);

void mackerel_addr_err_c(unsigned long fault_addr, unsigned long pc)
{
	pr_emerg("Address error: fault_addr=0x%08lx pc=0x%08lx\n",
		 fault_addr, pc);
	panic("Address error");
}

void mackerel_bus_err_c(unsigned long fault_addr, unsigned long pc)
{
	pr_emerg("Bus error: fault_addr=0x%08lx pc=0x%08lx\n",
		 fault_addr, pc);
	panic("Bus error");
}

extern void mackerel_bus_err(void);
extern void mackerel_addr_err(void);

void __init trap_init(void)
{
	int i;

	// Reset any bootloader-assigned exception handlers
	for (i = 0; i < 256; i++)
		_ramvec[i] = (e_vector)bad_interrupt;

	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

	// Autovectors
	_ramvec[25] = (e_vector)inthandler1;
	_ramvec[26] = (e_vector)inthandler2;
	_ramvec[27] = (e_vector)inthandler3;
	_ramvec[28] = (e_vector)inthandler4;
	_ramvec[29] = (e_vector)inthandler5;
	_ramvec[30] = (e_vector)inthandler6;
	_ramvec[31] = (e_vector)inthandler7;

	_ramvec[32] = system_call;
}

void __init init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++) {
		irq_set_chip(i, &intc_irq_chip);
		irq_set_handler(i, handle_level_irq);
	}
}
