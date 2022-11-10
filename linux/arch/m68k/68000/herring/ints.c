#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/herring.h>

asmlinkage void system_call(void);
asmlinkage irqreturn_t inthandler1(void);

static u8 counter = 0;

void process_int(int vec, struct pt_regs *fp)
{
    // Determine the type of interrupt
    u8 misr = MEM(DUART_MISR);

    if (misr & DUART_INTR_COUNTER)
    {
        // Counter timed out
        MEM(DUART_OPR_RESET); // Read the OPR port to clear the counter and reset the timer. This clears the interrupt

        MEM(DUART_OPR) = 0xFF;
        MEM(DUART_OPR_RESET) = counter;
        counter++;

        do_IRQ(1, fp);
    }

    if (misr & DUART_INTR_RXRDY)
    {
        // RX character available
        u8 a = MEM(DUART_RBB); // Read the available byte. This clears the interrupt
        // mputc(a);                   // Print the character back out

        do_IRQ(2, fp);
    }
}

static void intc_irq_unmask(struct irq_data *d)
{
    // TODO
}

static void intc_irq_mask(struct irq_data *d)
{
    // TODO
}

static struct irq_chip intc_irq_chip = {
    .name = "HERRING-INTC",
    .irq_mask = intc_irq_mask,
    .irq_unmask = intc_irq_unmask,
};

void __init trap_init(void)
{
    _ramvec[32] = system_call;
    _ramvec[0x40] = inthandler1;

    // TODO: map everything else to bad interrupt handler
}

void __init init_IRQ(void)
{
    int i;

    for (i = 0; (i < NR_IRQS); i++)
    {
        irq_set_chip(i, &intc_irq_chip);
        irq_set_handler(i, handle_level_irq);
    }
}
