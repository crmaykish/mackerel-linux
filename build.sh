export PATH=$PATH:/home/$(whoami)/x-tools/m68k-mackerel-linux-musl/bin

echo "Cleaning up previous builds..."
rm -rf image.bin rootfs
make clean


echo "Building the kernel..."
make ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- -j$(nproc)

echo "Creating binary image..."
m68k-linux-gnu-objcopy -O binary vmlinux image.bin

echo "_end:"
m68k-linux-gnu-nm vmlinux | grep ' _end$' | cut -d' ' -f1

echo "Done."
