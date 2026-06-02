#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
SYSTEM="m68k-mackerel-uclinux-uclibc"
export PATH=$PATH:$HOME/x-tools/"$SYSTEM"/bin
CROSS="$SYSTEM-"

echo "Dist clean..."
make ARCH=m68k distclean

echo "Defconfig..."
make ARCH=m68k mackerel10_defconfig

# Embed initramfs if the manifest exists (run build_initramfs_nommu.sh first)
if [ -f "$SCRIPT_DIR/initramfs.list" ]; then
    echo "Initramfs: embedding $SCRIPT_DIR/initramfs.list"
    ./scripts/config --set-str CONFIG_INITRAMFS_SOURCE "$SCRIPT_DIR/initramfs.list"
    ./scripts/config --set-val  CONFIG_INITRAMFS_ROOT_UID 0
    ./scripts/config --set-val  CONFIG_INITRAMFS_ROOT_GID 0
    make ARCH=m68k olddefconfig
else
    echo "Initramfs: none (run build_initramfs_nommu.sh to add one)"
fi

echo "Build kernel..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j$(nproc)

echo "Create binary..."
"${CROSS}"objcopy -O binary vmlinux image.bin

echo "_end:"
"${CROSS}"nm vmlinux | grep ' _end$' | cut -d' ' -f1

echo "Image size: $(du -h image.bin | cut -f1)"
echo "Done."
set +e
