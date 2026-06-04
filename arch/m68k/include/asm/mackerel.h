#ifndef _MACKEREL_H
#define _MACKEREL_H

#define MEM(address) (*(volatile unsigned char *)(address))

#ifdef CONFIG_MACKEREL08
#define MACKEREL_BOARD_NAME "Mackerel-08"
#define IRQ_NUM_DUART 1		// DUART on IPL1
#define DUART1_BASE 0x3FC000

#elif defined(CONFIG_MACKEREL10)
#define MACKEREL_BOARD_NAME "Mackerel-10"
#define IRQ_NUM_IDE   3
#define IRQ_NUM_DUART 5
#define IRQ_NUM_TIMER 6
#define DUART1_BASE 0xFF8000

#else   // Mackerel-30
#define MACKEREL_BOARD_NAME "Mackerel-30"
#define IRQ_NUM_DUART 5
// Mackerel-30 Timer
#define TIMER_BASE   0xF0030000
#define TIMER_ENABLE  (TIMER_BASE + 0x00)
#define TIMER_DISABLE (TIMER_BASE + 0x01)
#define DUART1_BASE 0xF0000000
#endif

#define DUART1_MR1A (DUART1_BASE + 0x01)
#define DUART1_MR2A (DUART1_BASE + 0x01)
#define DUART1_SRA (DUART1_BASE + 0x03)
#define DUART1_CSRA (DUART1_BASE + 0x03)
#define DUART1_CRA (DUART1_BASE + 0x05)
#define DUART1_MISR (DUART1_BASE + 0x05)
#define DUART1_RBA (DUART1_BASE + 0x07)
#define DUART1_TBA (DUART1_BASE + 0x07)
#define DUART1_IPCR (DUART1_BASE + 0x09)
#define DUART1_ACR (DUART1_BASE + 0x09)
#define DUART1_ISR (DUART1_BASE + 0x0B)
#define DUART1_IMR (DUART1_BASE + 0x0B)
#define DUART1_CUR (DUART1_BASE + 0x0D)
#define DUART1_CLR (DUART1_BASE + 0x0F)
#define DUART1_MR1B (DUART1_BASE + 0x11)
#define DUART1_MR2B (DUART1_BASE + 0x11)
#define DUART1_SRB (DUART1_BASE + 0x13)
#define DUART1_CSRB (DUART1_BASE + 0x13)
#define DUART1_CRB (DUART1_BASE + 0x15)
#define DUART1_RBB (DUART1_BASE + 0x17)
#define DUART1_TBB (DUART1_BASE + 0x17)
#define DUART1_IVR (DUART1_BASE + 0x19)
#define DUART1_IP (DUART1_BASE + 0x1B)
#define DUART1_OPCR (DUART1_BASE + 0x1B)
#define DUART1_OPR (DUART1_BASE + 0x1D)
#define DUART1_OPR_RESET (DUART1_BASE + 0x1F)

// IDE
#define IDE_BASE     0xF0010000
#define IDE_CTL_BASE 0xF0020000

// Interrupt bits
#define DUART_INTR_COUNTER 0b00001000
#define DUART_INTR_RXRDY 0b00100000

// Mackerel-08 serial interrupts and timer interrupts both come from the same DUART pin
// so we need to preserve IMR and ACR register bits during the timer tick
#ifdef CONFIG_MACKEREL08
#define DUART_IMR_RESERVED DUART_INTR_COUNTER
#define DUART_ACR_RESERVED 0x70

// The other Mackere boards have separate timer and serial interrupt lines, so not important
#else
#define DUART_IMR_RESERVED 0
#define DUART_ACR_RESERVED 0
#endif

// DUART
void duart_putc(char c);
void duart_puts(const char *s);
char duart_getc(void);

#endif /* _MACKEREL_H */
