/*
 * MTD map for the Mackerel-F XIP ROMfs region
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>

#define MACKF_ROMFS_PHYS 0x700000
#define MACKF_ROMFS_SIZE 0xc0000 // 768 KB (0x700000..0x7BFFFF)

static struct map_info mackf_map = {
	.name = "mackerel-romfs",
	.phys = MACKF_ROMFS_PHYS,
	.size  = MACKF_ROMFS_SIZE,
	.bankwidth = 1,
};

static struct mtd_info *mackf_mtd;

static int __init mackf_map_init(void)
{
	mackf_map.virt = ioremap(mackf_map.phys, mackf_map.size);
	if (!mackf_map.virt)
		return -EIO;

	simple_map_init(&mackf_map);

	mackf_mtd = do_map_probe("map_rom", &mackf_map);
	if (!mackf_mtd) {
		iounmap(mackf_map.virt);
		return -ENXIO;
	}

	mackf_mtd->owner = THIS_MODULE;
	mtd_device_register(mackf_mtd, NULL, 0);

	pr_info("mackerel-f: ROMfs MTD at 0x%06x, %u KB\n",
		MACKF_ROMFS_PHYS, MACKF_ROMFS_SIZE >> 10);
	return 0;
}
device_initcall(mackf_map_init);
