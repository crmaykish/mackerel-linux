#!/bin/sh

#rm -f image.bin
#m68k-mackerel-linux-gnu-objcopy -O binary vmlinux image.bin
sudo mount /dev/sdb1 /mnt/cf
sudo cp image.bin /mnt/cf/IMAGE.BIN
sync
sudo umount /mnt/cf
echo "Done."
