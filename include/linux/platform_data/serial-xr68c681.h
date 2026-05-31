#ifndef _PLATFORM_DATA_SERIAL_XR68C681_H
#define _PLATFORM_DATA_SERIAL_XR68C681_H

#include <linux/types.h>

#define XR68C681_MAX_UARTS 2

struct xr68c681_pdata {
	unsigned int nr_ports;
	unsigned int reg_shift;
	unsigned long uartclk;
};

#endif /* _PLATFORM_DATA_SERIAL_XR68C681_H */
