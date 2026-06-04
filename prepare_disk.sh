#!/usr/bin/env bash
set -e

# Prepare a storage device (CF, SD, etc.) for use with any Mackerel board.
#
# sda1 — FAT16, 16 MB  — boot partition (bootloader reads IMAGE.BIN from here)
# sda2 — ext4, 100 MB  — root filesystem (mounted at /)
#
# This only partitions and formats the card. Use install_disk.sh to write
# the kernel and root filesystem.

DRIVE="${1:-}"

if [ -z "$DRIVE" ]; then
    echo "Usage: sudo $0 <device> (e.g. /dev/sdb)"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (sudo $0 $*)"
    exit 1
fi

if [ ! -b "$DRIVE" ]; then
    echo "Error: $DRIVE is not a block device"
    exit 1
fi

BOOT_PART="${DRIVE}1"
ROOT_PART="${DRIVE}2"

echo "Warning! This will erase all data on $DRIVE"
read -p "Proceed? (yes/no): " confirm
if [[ "$confirm" != "yes" && "$confirm" != "y" ]]; then
    echo "Exiting."
    exit 1
fi

echo "Partitioning $DRIVE..."
parted --script "$DRIVE" mklabel msdos
parted --script "$DRIVE" mkpart primary fat16 0% 16MB
parted --script "$DRIVE" mkpart primary ext4 16MB 116MB

echo "Formatting partitions..."
mkfs.fat -F 16 "$BOOT_PART"
mkfs.ext4 "$ROOT_PART"

fdisk -l "$DRIVE"

echo
echo "Done! Layout:"
echo "$BOOT_PART (FAT16, 16 MB) — boot partition"
echo "$ROOT_PART (ext4, 100 MB) — root filesystem"
