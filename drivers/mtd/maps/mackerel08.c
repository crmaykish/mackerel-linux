/*
 * MTD map for the Mackerel-08 boot-ROM ROMfs region.
 * The 512 KB Mackerel-08 ROM is also used by the bootloader and some of the top address space
 * is reserved for the DUART, so only 432 KB is available for the ROMfs. 
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/mtd/map.h>
#include <linux/mtd/mtd.h>

#define MACK08_ROMFS_PHYS 0x390000
#define MACK08_ROMFS_SIZE 0x6c000 // 512 KB - 64 KB (bootloader) - 16 KB (DUART) = 432 KB

static struct map_info mack08_map = {
	.name = "mackerel-romfs",
	.phys = MACK08_ROMFS_PHYS,
	.size  = MACK08_ROMFS_SIZE,
	.bankwidth = 1,
};

static struct mtd_info *mack08_mtd;

static int __init mack08_map_init(void)
{
	mack08_map.virt = ioremap(mack08_map.phys, mack08_map.size);
	if (!mack08_map.virt)
		return -EIO;

	simple_map_init(&mack08_map);

	mack08_mtd = do_map_probe("map_rom", &mack08_map);
	if (!mack08_mtd) {
		iounmap(mack08_map.virt);
		return -ENXIO;
	}

	mack08_mtd->owner = THIS_MODULE;
	mtd_device_register(mack08_mtd, NULL, 0);

	pr_info("mackerel08: ROMfs MTD at 0x%06x, %u KB\n",
		MACK08_ROMFS_PHYS, MACK08_ROMFS_SIZE >> 10);
	return 0;
}
device_initcall(mack08_map_init);
