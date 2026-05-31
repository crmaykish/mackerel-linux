// SPDX-License-Identifier: GPL-2.0
/*
 * Bitbang SPI controller for the Mackerel-30 68030 SBC.
 *
 * Uses the XR68C681 DUART auxiliary port for GPIO:
 *   OP2 = MOSI, OP3 = SCLK, OP4 = CS0 (ENC28J60), OP5 = CS1
 *   IP4 = MISO
 *
 * SOPR (base+0x1F) sets output bits HIGH; ROPR (base+0x1D) sets them LOW.
 * IPR  (base+0x1B) reads the input port.
 *
 * Copyright (C) 2026 Colin Maykish <crmaykish@protonmail.com>
 */
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>
#include <linux/io.h>
#include <asm/irq.h>

#define DRV_NAME "spi-mackerel"

/* DUART register offsets from DUART1_BASE */
#define DUART_IPR_OFF   0x1B
#define DUART_ROPR_OFF  0x1D  /* write 1s → set bits LOW */
#define DUART_SOPR_OFF  0x1F  /* write 1s → set bits HIGH */

/* SPI GPIO bits */
#define SPI_MOSI_BIT  BIT(2)  /* OP2 */
#define SPI_SCLK_BIT  BIT(3)  /* OP3 */
#define SPI_MISO_BIT  BIT(4)  /* IP4 */

/* Chip select pin map: index → OP bit */
static const u8 cs_bits[] = {
	BIT(4),  /* CS0: OP4 — ENC28J60 */
	BIT(5),  /* CS1: OP5 — spare */
};

struct mackerel_spi {
	struct spi_bitbang  bitbang;
	void __iomem       *base;
};

static inline struct mackerel_spi *spidev_to_sp(struct spi_device *spi)
{
	return spi_controller_get_devdata(spi->controller);
}

static inline void setsck(struct spi_device *spi, int on)
{
	struct mackerel_spi *sp = spidev_to_sp(spi);

	writeb(SPI_SCLK_BIT, sp->base + (on ? DUART_SOPR_OFF : DUART_ROPR_OFF));
}

static inline void setmosi(struct spi_device *spi, int on)
{
	struct mackerel_spi *sp = spidev_to_sp(spi);

	writeb(SPI_MOSI_BIT, sp->base + (on ? DUART_SOPR_OFF : DUART_ROPR_OFF));
}

static inline int getmiso(struct spi_device *spi)
{
	struct mackerel_spi *sp = spidev_to_sp(spi);

	return !!(readb(sp->base + DUART_IPR_OFF) & SPI_MISO_BIT);
}

#define spidelay(nsecs) do { } while (0)

#include "spi-bitbang-txrx.h"

static u32 mackerel_txrx_mode0(struct spi_device *spi, unsigned int nsecs,
				u32 word, u8 bits, unsigned int flags)
{
	return bitbang_txrx_be_cpha0(spi, nsecs, 0, flags, word, bits);
}

static void mackerel_chipselect(struct spi_device *spi, int is_on)
{
	struct mackerel_spi *sp = spidev_to_sp(spi);
	u8 cs_idx = spi_get_chipselect(spi, 0);
	u8 cs_bit;

	if (cs_idx >= ARRAY_SIZE(cs_bits))
		return;

	cs_bit = cs_bits[cs_idx];

	/* CS is active low: ACTIVE → ROPR (LOW), INACTIVE → SOPR (HIGH) */
	if (is_on == BITBANG_CS_ACTIVE)
		writeb(cs_bit, sp->base + DUART_ROPR_OFF);
	else
		writeb(cs_bit, sp->base + DUART_SOPR_OFF);
}

static int mackerel_spi_probe(struct platform_device *pdev)
{
	struct mackerel_spi *sp;
	struct spi_controller *host;
	struct resource *res;
	int ret;

	host = spi_alloc_host(&pdev->dev, sizeof(*sp));
	if (!host)
		return -ENOMEM;

	sp = spi_controller_get_devdata(host);
	platform_set_drvdata(pdev, sp);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res) {
		ret = -ENODEV;
		goto err_put;
	}

	/*
	 * Use devm_ioremap rather than devm_ioremap_resource to avoid
	 * conflicting with the xr68c681 UART driver's region claim on the
	 * same DUART base address.
	 */
	sp->base = devm_ioremap(&pdev->dev, res->start, resource_size(res));
	if (!sp->base) {
		ret = -ENOMEM;
		goto err_put;
	}

	host->bus_num        = 0;
	host->num_chipselect = ARRAY_SIZE(cs_bits);
	host->mode_bits      = SPI_MODE_0;

	sp->bitbang.ctlr              = host;
	sp->bitbang.chipselect        = mackerel_chipselect;
	sp->bitbang.txrx_word[SPI_MODE_0] = mackerel_txrx_mode0;

	/* Idle state: all CS high, SCLK low, MOSI low */
	writeb(cs_bits[0] | cs_bits[1], sp->base + DUART_SOPR_OFF);
	writeb(SPI_SCLK_BIT | SPI_MOSI_BIT, sp->base + DUART_ROPR_OFF);

	ret = spi_bitbang_start(&sp->bitbang);
	if (ret) {
		dev_err(&pdev->dev, "spi_bitbang_start failed: %d\n", ret);
		goto err_put;
	}

	/* CS0: ENC28J60 Ethernet, interrupt on IRQ_AUTO_4 (CPLD PIN_10) */
	{
		struct spi_board_info enc_info = {
			.modalias     = "enc28j60",
			.max_speed_hz = 8000000,
			.chip_select  = 0,
			.mode         = SPI_MODE_0,
			.irq          = IRQ_AUTO_4,
		};
		if (!spi_new_device(host, &enc_info))
			dev_warn(&pdev->dev, "failed to create enc28j60 device\n");
	}

	dev_info(&pdev->dev, "Mackerel-30 SPI bitbang controller ready\n");
	return 0;

err_put:
	spi_controller_put(host);
	return ret;
}

static void mackerel_spi_remove(struct platform_device *pdev)
{
	struct mackerel_spi *sp = platform_get_drvdata(pdev);

	spi_bitbang_stop(&sp->bitbang);
	spi_controller_put(sp->bitbang.ctlr);
}

static struct platform_driver mackerel_spi_driver = {
	.probe  = mackerel_spi_probe,
	.remove = mackerel_spi_remove,
	.driver = {
		.name = DRV_NAME,
	},
};

module_platform_driver(mackerel_spi_driver);

MODULE_AUTHOR("Colin Maykish <crmaykish@protonmail.com>");
MODULE_DESCRIPTION("Mackerel-30 bitbang SPI controller (DUART GPIO)");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:" DRV_NAME);
