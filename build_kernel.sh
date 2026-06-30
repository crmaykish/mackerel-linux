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

# Append a ROMfs into image.bin for the MTD_UCLINUX boards (10, F)
# the romfs sits at __bss_start in the image
append_romfs() {
    local ROMFS="$1"

    if [ ! -f "$ROMFS" ]; then
        echo "Error: $ROMFS not found (run build_busybox.sh $BOARD + build_rootfs.sh $BOARD first)"
        exit 1
    fi

    # head.S expects the ROMfs to start exactly at __bss_start
    local bss_start
    bss_start=0x$("${CROSS}"nm vmlinux | awk '/ __bss_start$/{print $1}')

    echo "Padding kernel image to __bss_start and appending $ROMFS..."
    "${CROSS}"objcopy -O binary --pad-to="$bss_start" vmlinux image.bin
    cat "$ROMFS" >> image.bin
}

echo "Cleanup old image..."
rm -f image.bin

echo "Distclean..."
make ARCH=m68k distclean

echo "Defconfig ($DEFCONFIG)..."
make ARCH=m68k "$DEFCONFIG"

echo "Build kernel..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)"

echo "Create image..."
case "$BOARD" in
    10|f|F)
        append_romfs "$SCRIPT_DIR/rom.bin"
        ;;
    *)
        # Mackerel-08 and -30 do their own thing for the rootfs
        "${CROSS}"objcopy -O binary vmlinux image.bin
        ;;
esac

echo "Done! Image size: $(du -h image.bin | cut -f1)"
