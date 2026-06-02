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
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void trap3(void);
asmlinkage void trap4(void);
asmlinkage void trap5(void);
asmlinkage void trap6(void);
asmlinkage void trap7(void);
asmlinkage void trap8(void);
asmlinkage void trap9(void);
asmlinkage void trap10(void);
asmlinkage void trap11(void);
asmlinkage void trap12(void);
asmlinkage void trap13(void);
asmlinkage void trap14(void);
asmlinkage void trap15(void);
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

static void intc_irq_unmask(struct irq_data *d) {}
static void intc_irq_mask(struct irq_data *d) {}

static struct irq_chip intc_irq_chip = {
	.name      = "MACKEREL-INTC",
	.irq_mask  = intc_irq_mask,
	.irq_unmask = intc_irq_unmask,
};

/*
 * 68000 bus/address error frame at handler entry (before any C prologue):
 *   SP+0:  function code (word)
 *   SP+2:  fault address (long)
 *   SP+6:  instruction register (word)
 *   SP+8:  SR (word)
 *   SP+10: PC (long)
 *
 * Read fault_addr and PC into d0/d1 before touching SP, then call C handler.
 */
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

	/* Fill all vectors first so no bootloader handlers remain active */
	for (i = 0; i < 256; i++)
		_ramvec[i] = (e_vector)bad_interrupt;

	/* Exception vectors 2-3: bus/address error */
	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

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
