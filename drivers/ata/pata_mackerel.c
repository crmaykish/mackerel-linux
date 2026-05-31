/*
 * PATA driver for the Mackerel-30 memory-mapped IDE interface.
 *
 * The Mackerel-30 connects two 74HC245 bus transceivers between the 68030
 * data bus and the IDE cable. The upper byte (IDE D8-D15 -> m68k D24-D31)
 * is wired straight through. The lower byte (IDE D0-D7 -> m68k D16-D23)
 * has its bit order reversed: IDE D0 lands on m68k D23, IDE D7 on m68k D16.
 *
 * 8-bit register accesses use only the upper byte lane and are unaffected.
 * 16-bit data port transfers require per-word reversal of the low byte.
 *
 * This issue will be corrected in a future board revision, but for now we implement the necessary quirks in this driver.
 * 
 * Copyright (C) 2026 Colin Maykish <crmaykish@protonmail.com>
 * Based on pata_platform.c by Paul Mundt.
 * GPL v2.
 */
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/blkdev.h>
#include <linux/ata.h>
#include <linux/libata.h>
#include <linux/platform_device.h>
#include <linux/bitrev.h>
#include <scsi/scsi_host.h>

#define DRV_NAME "pata-mackerel"
#define DRV_VERSION "0.1"

/* IDE registers are at 2-byte stride (A1 selects the register). */
#define MACKEREL_IDE_SHIFT 1

static inline void mackerel_write_devctl(void __iomem *ctl_addr, u8 ctl)
{
	__raw_writew(bitrev8(ctl), ctl_addr);
}

static int pata_mackerel_set_mode(struct ata_link *link,
				  struct ata_device **unused)
{
	struct ata_device *dev;

	ata_for_each_dev(dev, link, ENABLED) {
		dev->pio_mode = dev->xfer_mode = XFER_PIO_0;
		dev->xfer_shift = ATA_SHIFT_PIO;
		dev->flags |= ATA_DFLAG_PIO;
		ata_dev_info(dev, "configured for PIO\n");
	}
	return 0;
}

static void pata_mackerel_dev_config(struct ata_device *dev)
{
	/* READ_NATIVE_MAX appears unreliable on this wiring; skip HPA logic. */
	dev->quirks |= ATA_QUIRK_BROKEN_HPA;
}

static void mackerel_sff_set_devctl(struct ata_port *ap, u8 ctl)
{
	/*
	 * Byte writes to ctl_addr (even) land on D31:D24 = IDE D15:D8 — the
	 * drive ignores them for Device Control (which is on IDE D0:D7).
	 * __raw_writew (= out_be16, no byteswap) puts bitrev8(ctl) in the low
	 * byte (D23:D16 = IDE D7:D0 via the bit-reversed 74HC245), delivering
	 * the correct value.  iowrite16 must not be used here: on big-endian
	 * m68k it calls cpu_to_le16 which swaps the bytes, landing data on
	 * the wrong lane.
	 */
	mackerel_write_devctl(ap->ioaddr.ctl_addr, ctl);
}

static int mackerel_softreset(struct ata_link *link, unsigned int *classes,
			      unsigned long deadline)
{
	struct ata_port *ap = link->ap;
	int rc;
	u8 err;

	ap->ops->sff_dev_select(ap, 0);

	mackerel_write_devctl(ap->ioaddr.ctl_addr, ap->ctl); udelay(20);
	mackerel_write_devctl(ap->ioaddr.ctl_addr, ap->ctl | ATA_SRST); udelay(20);
	mackerel_write_devctl(ap->ioaddr.ctl_addr, ap->ctl);
	ap->last_ctl = ap->ctl;

	rc = ata_sff_wait_after_reset(link, 1, deadline);
	if (rc && rc != -ENODEV) {
		ata_link_err(link, "SRST failed (errno=%d)\n", rc);
		return rc;
	}

	classes[0] = ata_sff_dev_classify(&link->device[0], 1, &err);
	classes[1] = ATA_DEV_NONE;

	return 0;
}

static unsigned int mackerel_sff_data_xfer(struct ata_queued_cmd *qc,
					   unsigned char *buf,
					   unsigned int buflen, int rw)
{
	struct ata_port *ap = qc->dev->link->ap;
	void __iomem *data = ap->ioaddr.data_addr;
	unsigned int words = buflen >> 1;
	u16 *buf16 = (u16 *)buf;
	unsigned int i;

	/*
	 * Use __raw_readw / __raw_writew (= out_be16 / in_be16, no byteswap).
	 * ioread16/iowrite16 call cpu_to_le16/le16_to_cpu which swap the bytes
	 * on big-endian m68k, putting the high/low IDE bytes on the wrong lanes.
	 * With __raw_readw the layout is: high byte = D31:D24 = IDE D15:D8,
	 * low byte = D23:D16 = bitrev8(IDE D0:D7) — exactly what the formulas
	 * below expect.
	 */
	if (rw == READ) {
		for (i = 0; i < words; i++) {
			u16 w = __raw_readw(data);
			buf16[i] = (w & 0xFF00) | bitrev8(w & 0xFF);
		}
	} else {
		for (i = 0; i < words; i++) {
			u16 w = buf16[i];
			__raw_writew((w & 0xFF00) | bitrev8(w & 0xFF), data);
		}
	}

	/* trailing odd byte — pad to a word */
	if (buflen & 1) {
		if (rw == READ) {
			u16 w = __raw_readw(data);
			buf[buflen - 1] = bitrev8(w & 0xFF);
		} else {
			__raw_writew(bitrev8(buf[buflen - 1]), data);
		}
	}

	return buflen;
}

static void pata_mackerel_setup_port(struct ata_ioports *ioaddr)
{
	unsigned int s = MACKEREL_IDE_SHIFT;

	ioaddr->data_addr    = ioaddr->cmd_addr + (ATA_REG_DATA    << s);
	ioaddr->error_addr   = ioaddr->cmd_addr + (ATA_REG_ERR     << s);
	ioaddr->feature_addr = ioaddr->cmd_addr + (ATA_REG_FEATURE << s);
	ioaddr->nsect_addr   = ioaddr->cmd_addr + (ATA_REG_NSECT   << s);
	ioaddr->lbal_addr    = ioaddr->cmd_addr + (ATA_REG_LBAL    << s);
	ioaddr->lbam_addr    = ioaddr->cmd_addr + (ATA_REG_LBAM    << s);
	ioaddr->lbah_addr    = ioaddr->cmd_addr + (ATA_REG_LBAH    << s);
	ioaddr->device_addr  = ioaddr->cmd_addr + (ATA_REG_DEVICE  << s);
	ioaddr->status_addr  = ioaddr->cmd_addr + (ATA_REG_STATUS  << s);
	ioaddr->command_addr = ioaddr->cmd_addr + (ATA_REG_CMD     << s);
}

static const struct scsi_host_template pata_mackerel_sht = {
	ATA_PIO_SHT(DRV_NAME),
};

static struct ata_port_operations pata_mackerel_port_ops = {
	.inherits       = &ata_sff_port_ops,
	.cable_detect   = ata_cable_unknown,
	.dev_config     = pata_mackerel_dev_config,
	.set_mode       = pata_mackerel_set_mode,
	.sff_data_xfer  = mackerel_sff_data_xfer,
	.sff_set_devctl = mackerel_sff_set_devctl,
	.reset.softreset = mackerel_softreset,
};

static irqreturn_t mackerel_sff_interrupt(int irq, void *dev_instance)
{
	struct ata_host *host = dev_instance;
	irqreturn_t rc;
	unsigned long flags;
	unsigned int i;

	rc = ata_sff_interrupt(irq, dev_instance);
	if (rc != IRQ_NONE)
		return rc;

	/*
	 * Bring-up fallback: if libata didn't claim IRQ3, clear status on all
	 * ports and still report handled to avoid "irq nobody cared" shutdown.
	 */
	spin_lock_irqsave(&host->lock, flags);
	for (i = 0; i < host->n_ports; i++) {
		struct ata_port *ap = host->ports[i];

		if (!ap)
			continue;
		if (ap->ops && ap->ops->sff_check_status)
			ap->ops->sff_check_status(ap);
		if (ap->ops && ap->ops->sff_irq_clear)
			ap->ops->sff_irq_clear(ap);
	}
	spin_unlock_irqrestore(&host->lock, flags);

	return IRQ_HANDLED;
}

static int pata_mackerel_probe(struct platform_device *pdev)
{
	struct resource *io_res, *ctl_res, *irq_res;
	struct ata_host *host;
	struct ata_port *ap;
	int irq = 0;

	io_res = platform_get_mem_or_io(pdev, 0);
	if (!io_res) {
		dev_err(&pdev->dev, "no IO resource\n");
		return -EINVAL;
	}

	ctl_res = platform_get_mem_or_io(pdev, 1);
	if (!ctl_res) {
		dev_err(&pdev->dev, "no CTL resource\n");
		return -EINVAL;
	}

	irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (irq_res)
		irq = irq_res->start;

	host = ata_host_alloc(&pdev->dev, 1);
	if (!host)
		return -ENOMEM;

	ap = host->ports[0];
	ap->ops = &pata_mackerel_port_ops;

	ap->pio_mask = ATA_PIO0;

	if (!irq) {
		ap->flags |= ATA_FLAG_PIO_POLLING;
		ata_port_desc(ap, "no IRQ, using PIO polling");
	}

	ap->ioaddr.cmd_addr = devm_ioremap(&pdev->dev, io_res->start,
					   resource_size(io_res));
	ap->ioaddr.ctl_addr = devm_ioremap(&pdev->dev, ctl_res->start,
					   resource_size(ctl_res));

	if (!ap->ioaddr.cmd_addr || !ap->ioaddr.ctl_addr) {
		dev_err(&pdev->dev, "failed to ioremap IO/CTL base\n");
		return -ENOMEM;
	}

	/*
	 * Treat ALTSTATUS as unavailable for now.
	 * CTL readback is unreliable on this wiring and can mislead IRQ logic.
	 */
	ap->ioaddr.altstatus_addr = NULL;

	pata_mackerel_setup_port(&ap->ioaddr);

	ata_port_desc(ap, "mmio cmd 0x%llx ctl 0x%llx",
		      (unsigned long long)io_res->start,
		      (unsigned long long)ctl_res->start);

	return ata_host_activate(host, irq,
				 irq ? mackerel_sff_interrupt : NULL,
				 irq ? IRQF_SHARED : 0,
				 &pata_mackerel_sht);
}

static struct platform_driver pata_mackerel_driver = {
	.probe  = pata_mackerel_probe,
	.remove = ata_platform_remove_one,
	.driver = {
		.name = DRV_NAME,
	},
};

module_platform_driver(pata_mackerel_driver);

MODULE_AUTHOR("Colin Maykish <crmaykish@protonmail.com>");
MODULE_DESCRIPTION("Mackerel-30 PATA driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRV_VERSION);
MODULE_ALIAS("platform:" DRV_NAME);
