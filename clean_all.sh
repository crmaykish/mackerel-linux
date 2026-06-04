#!/usr/bin/env bash
set -e

echo "Distclean"
make distclean

echo "Kernel image"
rm -f image.bin

echo "Filesystem images"
rm -f romfs.img sd.img

echo "BusyBox binaries"
rm -f busybox busybox_mackerel08 busybox_nommu

echo "Filesystems"
sudo rm -rf rootfs_mackerel30 romfs_mackerel08 romfs_mackerel30 \
        initramfs initramfs.list

echo "BusyBox scratch build dirs"
rm -rf .busybox-30-build .busybox-10-build .busybox-08-build .busybox-nommu-build

echo "Done."
