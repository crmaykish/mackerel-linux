// Platform data for the Mackerel memory-mapped PATA interface.
#ifndef _PLATFORM_DATA_PATA_MACKEREL_H
#define _PLATFORM_DATA_PATA_MACKEREL_H

#include <linux/types.h>

struct pata_mackerel_pdata {
	/*
	 * Mackerel-30 reaches the IDE bus through 74HC245 buffers that reverse
	 * the bit order of the low data byte; 16-bit data words and the
	 * device-control register must be compensated in software, and the
	 * control register read-back is unreliable.
	 *
	 * Mackerel-10 wires the bus straight through, so standard byte/word
	 * accesses work. Leave this false for such boards.
	 */
	bool low_byte_bitrev;
};

#endif /* _PLATFORM_DATA_PATA_MACKEREL_H */
