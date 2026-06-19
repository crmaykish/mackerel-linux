// SPDX-License-Identifier: GPL-2.0
// Mackerel-F low-level uart16550 I/O (raw console helpers)
// Bootloader leaves the UART initialized, just use the same settings (115200 baud, 8 data bits)

#include <asm/mackerel.h>

void uart16550_putc(char c)
{
	while ((MEM(MF_UART_LSR) & MF_LSR_THRE) == 0) {}
	MEM(MF_UART_THR) = c;
	if (c == '\n')
		uart16550_putc('\r');
}

char uart16550_getc(void)
{
	while ((MEM(MF_UART_LSR) & MF_LSR_DR) == 0) {}
	return MEM(MF_UART_RBR);
}
