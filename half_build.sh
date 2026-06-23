export PATH=$PATH:/home/$(whoami)/x-tools/m68k-mackerel-linux-musl/bin



echo "Building the kernel..."
make ARCH=m68k CROSS_COMPILE=m68k-linux-gnu- -j$(nproc)

echo "Creating binary image..."
m68k-linux-gnu-objcopy -O binary vmlinux image.bin

echo "_end:"
m68k-linux-gnu-nm vmlinux | grep ' _end$' | cut -d' ' -f1

echo "Done."
