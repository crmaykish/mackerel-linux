#!/usr/bin/env bash
# Compile the appropriate busybox binary for the chosen Mackerel board
# .config files are in busybox_configs/
set -e

BOARD="${1:-30}"
BUSYBOX_VERSION=1.36.1
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

case "$BOARD" in
    30) SYSTEM=m68k-mackerel-linux-musl     ; LINK=dynamic ; OUT="$SCRIPT_DIR/busybox" ;;
    10) SYSTEM=m68k-mackerel-uclinux-uclibc ; LINK=bflt    ; OUT="$SCRIPT_DIR/busybox_nommu" ;;
    08) SYSTEM=m68k-mackerel-uclinux-uclibc ; LINK=bflt    ; OUT="$SCRIPT_DIR/busybox_mackerel08" ;;
    f|F)  SYSTEM=m68k-mackerel-uclinux-uclibc ; LINK=bflt    ; OUT="$SCRIPT_DIR/busybox_mackerelf" ;;
    *)  echo "Usage: $0 [board]   (board: 30, 10, 08, or F; default 30)"; exit 1 ;;
esac

export PATH=$PATH:$HOME/x-tools/"$SYSTEM"/bin
CROSS="$SYSTEM-"
DEFCONFIG="$SCRIPT_DIR/busybox_configs/mackerel${BOARD}_defconfig"
BUILD_DIR="$SCRIPT_DIR/.busybox-${BOARD}-build"

if [ ! -f "$DEFCONFIG" ]; then
    echo "Error: $DEFCONFIG not found"
    exit 1
fi

cleanup() { rm -rf "$BUILD_DIR"; }
trap cleanup EXIT

# Fetch (cached) and unpack — busybox.net is slow, so cache the tarball
CACHE_DIR="$HOME/src"
TARBALL="busybox-${BUSYBOX_VERSION}.tar.bz2"
mkdir -p "$CACHE_DIR"
if ! bzip2 -t "$CACHE_DIR/$TARBALL" 2>/dev/null; then
    echo "Downloading BusyBox ${BUSYBOX_VERSION}..."
    wget -c -q --show-progress -O "$CACHE_DIR/$TARBALL" "$BUSYBOX_URL"
fi

echo "Preparing build directory..."
rm -rf "$BUILD_DIR"; mkdir -p "$BUILD_DIR"; cd "$BUILD_DIR"
cp "$CACHE_DIR/$TARBALL" .; tar xf "$TARBALL"; cd "busybox-${BUSYBOX_VERSION}"

echo "Applying mackerel${BOARD}_defconfig..."
cp "$DEFCONFIG" "configs/mackerel${BOARD}_defconfig"
make ARCH=m68k CROSS_COMPILE="$CROSS" "mackerel${BOARD}_defconfig"

if [ "$LINK" = "dynamic" ]; then
    echo "Building..."
    make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)"
    cp busybox "$OUT"
    "${CROSS}strip" "$OUT"
else
    echo "Building busybox_unstripped (cross-strip mishandles bFLT)..."
    make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)" busybox_unstripped
    file busybox_unstripped | grep -q "BFLT" || {
        echo "ERROR: busybox_unstripped is not bFLT!"; file busybox_unstripped; exit 1; }
    cp busybox_unstripped "$OUT"
    [ -f busybox_unstripped.gdb ] && cp busybox_unstripped.gdb "$OUT.gdb"
fi

echo "Done: $OUT"
file "$OUT"
ls -lh "$OUT"
"${CROSS}size" "$OUT" 2>/dev/null || true
