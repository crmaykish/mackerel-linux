#include <asm/mackerel.h>

void duart_putc(char c)
{
    while ((MEM(DUART1_SRB) & 0b00000100) == 0)
    {
    }

    MEM(DUART1_TBB) = c;

    if (c == 0x0A)
    {
        duart_putc(0x0D);
    }
}

char duart_getc(void)
{
    while ((MEM(DUART1_SRB) & 0b00000001) == 0)
    {
    }

    return MEM(DUART1_RBB);
}

void duart_puts(const char *s)
{
    unsigned i = 0;

    while (s[i] != 0)
    {
        duart_putc(s[i]);
        i++;
    }
}
