// SPDX-License-Identifier: GPL-2.0
// Mackerel-F board init and early console.

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/console.h>
#include <linux/platform_device.h>
#include <linux/serial_8250.h>
#include <linux/serial_core.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_oc_tiny.h>
#include <linux/gpio/machine.h>
#include <linux/property.h>
#include <linux/platform_data/wiznet.h>
#include <asm/machdep.h>
#include <asm/mackerel.h>
#include <asm/traps.h>

extern void legacy_timer_tick(unsigned long ticks);

static irqreturn_t mackerelf_tick(int irq, void *dummy)
{
	// Read the Timer STATUS register to clear the interrupt
	MEM(TIMER_STATUS) = 0;
	legacy_timer_tick(1);
	return IRQ_HANDLED;
}

static void mackerelf_sched_init(void)
{
	int ret;

	// Setup the timer interrupt
	ret = request_irq(IRQ_NUM_TIMER, mackerelf_tick, IRQF_TIMER, "timer", NULL);
	if (ret) {
		pr_err("Mackerel-F: cannot get timer IRQ: %d\n", ret);
		return;
	}

	// Start the timer
	MEM(TIMER_CTRL) = TIMER_ENABLE_100HZ;
}

static void mackerelf_console_write(struct console *co, const char *str,
				    unsigned int count)
{
	unsigned int i;

	for (i = 0; i < count && str[i]; i++)
		uart16550_putc(str[i]);
}

static struct console mackerelf_console_driver = {
	.name  = "mackconsole",
	.flags = CON_PRINTBUFFER | CON_BOOT,
	.index = -1,
	.write = mackerelf_console_write,
};

static void mackerelf_reset(void)
{
	local_irq_disable();
}

static struct plat_serial8250_port mackerelf_uart_port[] = {
	{
		.mapbase  = UART_BASE,
		.membase  = (unsigned char __iomem *)UART_BASE,
		.irq      = IRQ_NUM_UART,
		.uartclk  = 64800000,
		.regshift = 1,
		.iotype   = UPIO_MEM,
		.type     = PORT_16550A,
		.flags    = UPF_BOOT_AUTOCONF | UPF_SKIP_TEST | UPF_FIXED_TYPE,
	},
	{ },
};

static struct platform_device mackerelf_uart_device = {
	.name = "serial8250",
	.id   = PLAT8250_DEV_PLATFORM,
	.dev  = {
		.platform_data = mackerelf_uart_port,
	},
};

static struct resource mackerelf_gpio_res[] = {
	{
		.name  = "dat",
		.start = GPIO_BASE,
		.end   = GPIO_BASE,
		.flags = IORESOURCE_MEM,
	},
};

static const struct property_entry mackerelf_gpio_props[] = {
	PROPERTY_ENTRY_STRING("label", "mackerelf-gpio"),
	{ }
};

static const struct platform_device_info mackerelf_gpio_info = {
	.name       = "basic-mmio-gpio",
	.id         = PLATFORM_DEVID_NONE,
	.res        = mackerelf_gpio_res,
	.num_res    = ARRAY_SIZE(mackerelf_gpio_res),
	.properties = mackerelf_gpio_props,
};

static struct tiny_spi_platform_data mackerelf_spi_pdata = {
	.freq      = 64800000,	// FPGA base clock - 64.8 MHz
	.baudwidth = 8,
};

static struct resource mackerelf_spi_res[] = {
	{
		.start = SPI_BASE,
		.end   = SPI_BASE + 0x1f,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device mackerelf_spi_device = {
	.name = "spi_oc_tiny",
	.id   = 0,	// spi0
	.dev  = {
		.platform_data = &mackerelf_spi_pdata,
	},
	.resource      = mackerelf_spi_res,
	.num_resources = ARRAY_SIZE(mackerelf_spi_res),
};

static struct gpiod_lookup_table mackerelf_spi_cs_gpios = {
	.dev_id = "spi0",
	.table  = {
		GPIO_LOOKUP_IDX("mackerelf-gpio", 6, "cs", 0, GPIO_ACTIVE_HIGH),
		{ }
	},
};

// 2nd tiny_spi controller (slot 4) for the W5500 NIC
static struct resource mackerelf_spi2_res[] = {
	{
		.start = SPI2_BASE,
		.end   = SPI2_BASE + 0x1f,
		.flags = IORESOURCE_MEM,
	},
};

static struct platform_device mackerelf_spi2_device = {
	.name = "spi_oc_tiny",
	.id   = 1,	// spi1
	.dev  = {
		.platform_data = &mackerelf_spi_pdata,
	},
	.resource      = mackerelf_spi2_res,
	.num_resources = ARRAY_SIZE(mackerelf_spi2_res),
};

static struct gpiod_lookup_table mackerelf_spi2_cs_gpios = {
	.dev_id = "spi1",
	.table  = {
		GPIO_LOOKUP_IDX("mackerelf-gpio", 7, "cs", 0, GPIO_ACTIVE_HIGH),
		{ }
	},
};

static struct wiznet_platform_data mackerelf_w5500_pdata = {
	.link_gpio = -1,
	.mac_addr  = { 0x02, 0x4d, 0x4b, 0x52, 0x46, 0x01 },
};

static struct spi_board_info mackerelf_spi_board_info[] = {
	{
		.modalias     = "mmc-spi-slot",
		.max_speed_hz = 8000000,
		.bus_num      = 0,
		.chip_select  = 0,
		.mode         = SPI_MODE_0,
	},
	{
		.modalias       = "w5500",
		.max_speed_hz   = 4000000,
		.bus_num        = 1,
		.chip_select    = 0,
		.mode           = SPI_MODE_0,
		.irq            = IRQ_NUM_NIC,	// W5500 INT, autovector level 4
		.platform_data  = &mackerelf_w5500_pdata,
	},
};

extern void mackerel_addr_err(void);
extern void mackerel_bus_err(void);

void __init config_BSP(char *command, int len)
{
	pr_info(MACKEREL_BOARD_NAME " support by Colin Maykish <crmaykish@protonmail.com>\n");

	// Early exception handlers
	_ramvec[2] = (e_vector)mackerel_bus_err;
	_ramvec[3] = (e_vector)mackerel_addr_err;

	mach_reset      = mackerelf_reset;
	mach_sched_init = mackerelf_sched_init;

	register_console(&mackerelf_console_driver);
}

static int __init mackerelf_platform_init(void)
{
	gpiod_add_lookup_table(&mackerelf_spi_cs_gpios);
	gpiod_add_lookup_table(&mackerelf_spi2_cs_gpios);
	spi_register_board_info(mackerelf_spi_board_info,
				ARRAY_SIZE(mackerelf_spi_board_info));

	if (platform_device_register(&mackerelf_uart_device))
		pr_err("Mackerel-F: could not register UART device\n");

	if (IS_ERR(platform_device_register_full(&mackerelf_gpio_info)))
		pr_err("Mackerel-F: could not register GPIO device\n");

	if (platform_device_register(&mackerelf_spi_device))
		pr_err("Mackerel-F: could not register SPI device\n");

	if (platform_device_register(&mackerelf_spi2_device))
		pr_err("Mackerel-F: could not register SPI2 device\n");

	return 0;
}
arch_initcall(mackerelf_platform_init);
