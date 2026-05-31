set -e

SYSTEM="m68k-mackerel-linux-musl"
export PATH=$PATH:/home/$(whoami)/x-tools/"$SYSTEM"/bin
CROSS="$SYSTEM-"

echo "Cleanup artifacts..."
rm -rf image.bin

echo "Dist clean..."
make distclean

echo "Defconfig..."
make ARCH=m68k mackerel30_defconfig

echo "Build the kernel..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j$(nproc)

echo "Create binary..."
"$CROSS"objcopy -O binary vmlinux image.bin

echo "_end:"
"$CROSS"nm vmlinux | grep ' _end$' | cut -d' ' -f1

echo "Done."

set +e
