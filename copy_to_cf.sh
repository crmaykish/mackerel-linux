#!/bin/sh
echo "Mount CF card..."
sudo mount /dev/sda1 /mnt/sd

echo "Copy Linux image..."
sudo cp image.bin /mnt/sd/IMAGE.BIN
sync

echo "Unmount CF card..."
sudo umount /mnt/sd

echo "Done."
