// SPDX-License-Identifier: GPL-2.0
// Mackerel-10 low-level DUART I/O

#include <asm/mackerel.h>

void duart_putc(char c)
{
	while ((MEM(DUART1_SRB) & 0x04) == 0)
		;
	MEM(DUART1_TBB) = c;
	if (c == '\n')
		duart_putc('\r');
}

char duart_getc(void)
{
	while ((MEM(DUART1_SRB) & 0x01) == 0)
		;
	return MEM(DUART1_RBB);
}
