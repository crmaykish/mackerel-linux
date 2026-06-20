#!/usr/bin/env bash
set -e

echo "Distclean"
make distclean

echo "Artifacts"
rm -f *.img *.bin *.gdb

echo "BusyBox"
rm -f busybox busybox_mackerel08 busybox_nommu busybox_mackerelf

echo "Filesystems"
sudo rm -rf rootfs_mackerel30 romfs_mackerel08 romfs_mackerel10 romfs_mackerelf initramfs initramfs.list

echo "BusyBox scratch build dirs"
rm -rf .busybox-30-build .busybox-10-build .busybox-08-build .busybox-nommu-build

echo "Done."
