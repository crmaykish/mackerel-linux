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
#define IDE_BASE     0xFFC000	// command block (CS0)
#define IDE_CTL_BASE 0xFF400C	// control block (CS1): alt status / device control

#elif defined(CONFIG_MACKERELF)
#define MACKEREL_BOARD_NAME "Mackerel-F"
#define GPIO_BASE  0xFFF800
#define UART_BASE  0xFFF900
#define TIMER_BASE 0xFFFA00
#define SPI_BASE   0xFFFB00
#define IRQ_NUM_UART  5
#define IRQ_NUM_TIMER 6

// Programmable Timer Registers
#define TIMER_CTRL   (TIMER_BASE + 0)
#define TIMER_STATUS (TIMER_BASE + 2)
#define TIMER_ENABLE_10HZ  (0x01 | (0 << 4))
#define TIMER_ENABLE_25HZ  (0x01 | (1 << 4))
#define TIMER_ENABLE_50HZ  (0x01 | (2 << 4))
#define TIMER_ENABLE_100HZ (0x01 | (3 << 4))

// 16550 Registers
#define UART_THR (UART_BASE + 0)
#define UART_RBR (UART_BASE + 0)
#define UART_DLL (UART_BASE + 0)
#define UART_IER (UART_BASE + 2)
#define UART_DLM (UART_BASE + 2)
#define UART_IIR (UART_BASE + 4)
#define UART_FCR (UART_BASE + 4)
#define UART_LCR (UART_BASE + 6)
#define UART_LSR (UART_BASE + 10)
#define LSR_DR   0x01
#define LSR_THRE 0x20

void uart16550_putc(char c);
char uart16550_getc(void);

#else   // Mackerel-30
#define MACKEREL_BOARD_NAME "Mackerel-30"
#define IRQ_NUM_DUART 5
// Mackerel-30 Timer
#define TIMER_BASE   0xF0030000
#define TIMER_ENABLE  (TIMER_BASE + 0x00)
#define TIMER_DISABLE (TIMER_BASE + 0x01)
#define DUART1_BASE 0xF0000000
#define IDE_BASE     0xF0010000
#define IDE_CTL_BASE 0xF0020000
#endif

#ifndef CONFIG_MACKERELF
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
#endif /* !CONFIG_MACKERELF */

#endif /* _MACKEREL_H */
