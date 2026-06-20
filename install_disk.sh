#!/usr/bin/env bash
set -e

# Copy the kernel and root filesystem to the target device.
# This script assumes the device is already partitioned with the prepare_disk.sh script

DRIVE="${1:-}"
BOARD="${2:-30}"

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
MOUNT_POINT="$(mktemp -d /tmp/mackerel-mnt.XXXXXX)"

cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        umount "$MOUNT_POINT"
    fi
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}
trap cleanup EXIT

if [ -z "$DRIVE" ]; then
    echo "Usage: sudo $0 <device> [board]   (board: 30|10|08|f, default 30)"
    exit 1
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (sudo $0 $*)"
    exit 1
fi

if [ ! -f "$SCRIPT_DIR/image.bin" ]; then
    echo "Error: image.bin not found at $SCRIPT_DIR/image.bin (build the kernel first)"
    exit 1
fi

# Mackerel-F's root is an XIP ROMfs loaded from the boot partition, so it needs
# romf.bin alongside the kernel (build it with build_rootfs.sh f).
if { [ "$BOARD" = "f" ] || [ "$BOARD" = "F" ]; } && [ ! -f "$SCRIPT_DIR/romf.bin" ]; then
    echo "Error: romf.bin not found at $SCRIPT_DIR/romf.bin (run build_rootfs.sh f first)"
    exit 1
fi

BOOT_PART="${DRIVE}1"
ROOT_PART="${DRIVE}2"

echo "Copying kernel image to $BOOT_PART..."
mount "$BOOT_PART" "$MOUNT_POINT"
cp "$SCRIPT_DIR/image.bin" "$MOUNT_POINT/IMAGE.BIN"
if [ "$BOARD" = "f" ] || [ "$BOARD" = "F" ]; then
    echo "Copying ROMfs root to $BOOT_PART..."
    cp "$SCRIPT_DIR/romf.bin" "$MOUNT_POINT/ROMFS.BIN"
fi
sync
umount "$MOUNT_POINT"

case "$BOARD" in
    30)
        # Mackerel-30
        ROOTFS="$SCRIPT_DIR/rootfs_mackerel30"
        echo "Copying rootfs to $ROOT_PART..."
        mount "$ROOT_PART" "$MOUNT_POINT"
        cp -a "$ROOTFS/." "$MOUNT_POINT/"
        chown -R 0:0 "$MOUNT_POINT"
        sync
        umount "$MOUNT_POINT"
        ;;
    10)
        # Mackerel-10
        echo "Skipping rootfs for Mackerel-10 (not supported yet)"
        ;;
    08)
        # Mackerel-08
        echo "Skipping rootfs for Mackerel-08 (not supported yet)"
        ;;
    f|F)
        echo "Mackerel-F root is the XIP ROMfs on $BOOT_PART (romf.bin); $ROOT_PART unused."
        ;;
    *)
        echo "Error: Invalid board '$BOARD' (expected 30, 10, 08, or F)"
        exit 1
        ;;
esac

echo "Done! Drive is ready to boot in Mackerel-$BOARD."
