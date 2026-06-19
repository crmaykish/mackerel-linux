#!/usr/bin/env bash

# Do a full clean and rebuild of the kernel image

set -e

BOARD="${1:-30}"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

case "$BOARD" in
    30) SYSTEM="m68k-mackerel-linux-musl"    ; DEFCONFIG="mackerel30_defconfig" ;;
    10) SYSTEM="m68k-mackerel-uclinux-uclibc"; DEFCONFIG="mackerel10_defconfig" ;;
    08) SYSTEM="m68k-mackerel-uclinux-uclibc"; DEFCONFIG="mackerel08_defconfig" ;;
    f|F)  SYSTEM="m68k-mackerel-uclinux-uclibc"; DEFCONFIG="mackerelf_defconfig" ;;
    *)  echo "Usage: $0 [board]   (board: 30, 10, 08, or f; default 30)"; exit 1 ;;
esac

export PATH=$PATH:$HOME/x-tools/"$SYSTEM"/bin
CROSS="$SYSTEM-"

# Concatenate ROMfs to the end of the kernel image (Mackerel-08)
append_romfs() {
    local ROMFS="$SCRIPT_DIR/romfs.img"

    if [ ! -f "$ROMFS" ]; then
        echo "Error: romfs.img not found (run build_busybox.sh 08 + build_rootfs.sh 08 first)"
        return 1
    fi

    # head.S code expects the ROMfs to start exactly at __bss_start
    # Use objcopy to pad the kernel image and then append the ROMfs
    local bss_start
    bss_start=0x$("${CROSS}"nm vmlinux | awk '/ __bss_start$/{print $1}')

    echo "Padding kernel image to align ROMds..."
    "${CROSS}"objcopy -O binary --pad-to="$bss_start" vmlinux image.bin

    echo "Appending $ROMFS to image.bin..."
    cat "$ROMFS" >> image.bin
}

echo "Cleanup old image..."
rm -f image.bin

echo "Distclean..."
make ARCH=m68k distclean

echo "Defconfig ($DEFCONFIG)..."
make ARCH=m68k "$DEFCONFIG"

# Mackerel-10 and Mackerel-F embed an initramfs, so it must exist before building
if { [ "$BOARD" = "10" ] || [ "$BOARD" = "f" ] || [ "$BOARD" = "F" ]; } && \
   [ ! -f "$SCRIPT_DIR/initramfs.list" ]; then
    echo "Error: initramfs.list not found (run build_rootfs.sh $BOARD first)"
    exit 1
fi

echo "Build kernel..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)"

echo "Create image..."
"${CROSS}"objcopy -O binary vmlinux image.bin

# NOTE: Mackerel-08 includes its ROMfs in the 512KB Flash ROM, not in the kernel image

echo "Done! Image size: $(du -h image.bin | cut -f1)"
