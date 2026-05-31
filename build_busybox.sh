#!/usr/bin/env bash
# Build a dynamically linked BusyBox binary for m68k-mackerel-linux-musl.
# Output: ./busybox in the repo root.
set -e

BUSYBOX_VERSION=1.36.1
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2"

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
BUILD_DIR="$(mktemp -d /tmp/busybox-build.XXXXXX)"
CROSS=m68k-mackerel-linux-musl-

cleanup() { rm -rf "$BUILD_DIR"; }
trap cleanup EXIT

export PATH=$PATH:~/x-tools/m68k-mackerel-linux-musl/bin

echo "[*] Downloading BusyBox ${BUSYBOX_VERSION}..."
cd "$BUILD_DIR"
wget -q --show-progress "$BUSYBOX_URL"
tar xf "busybox-${BUSYBOX_VERSION}.tar.bz2"
cd "busybox-${BUSYBOX_VERSION}"

echo "[*] Configuring..."
make ARCH=m68k CROSS_COMPILE="$CROSS" defconfig

# Helpers to set/enable/disable Kconfig options in .config
cfg_enable()  {
    local o="$1"
    if grep -q "^# CONFIG_${o} is not set" .config; then
        sed -i "s|^# CONFIG_${o} is not set|CONFIG_${o}=y|" .config
    elif grep -q "^CONFIG_${o}=" .config; then
        sed -i "s|^CONFIG_${o}=.*|CONFIG_${o}=y|" .config
    else
        echo "CONFIG_${o}=y" >> .config
    fi
}
cfg_disable() {
    local o="$1"
    if grep -q "^CONFIG_${o}=" .config; then
        sed -i "s|^CONFIG_${o}=.*|# CONFIG_${o} is not set|" .config
    fi
}
cfg_str() {
    local o="$1" v="$2"
    if grep -q "^CONFIG_${o}=" .config; then
        sed -i "s|^CONFIG_${o}=.*|CONFIG_${o}=\"${v}\"|" .config
    else
        echo "CONFIG_${o}=\"${v}\"" >> .config
    fi
}

# Dynamic linking
cfg_disable STATIC
cfg_disable STATIC_LIBGCC

cfg_enable UDHCPC
cfg_enable UDHCPC6
cfg_enable FEATURE_UDHCPC_ARPING
cfg_str    UDHCPC_DEFAULT_SCRIPT "/usr/share/udhcpc/default.script"
cfg_enable ROUTE
cfg_enable IP
cfg_enable ARPING
cfg_enable TRACEROUTE
cfg_enable TRACEROUTE6
cfg_enable NSLOOKUP
cfg_enable ARP
cfg_enable NC
cfg_enable WGET
cfg_enable PING
cfg_enable RDATE
cfg_enable SYSLOGD
cfg_enable KLOGD

# Disable applets that fail to build against musl headers
cfg_disable TC

# Sync config defaults for any new dependencies
yes "" | make ARCH=m68k CROSS_COMPILE="$CROSS" oldconfig

# oldconfig reverts options that differ from defconfig defaults back to n when
# answered by `yes ""` — re-apply anything that must survive the pass.
cfg_disable STATIC
cfg_disable TC

echo "[*] Building..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)"

echo "[*] Copying output..."
cp busybox "$SCRIPT_DIR/busybox"
"${CROSS}strip" "$SCRIPT_DIR/busybox"

echo
echo "[+] Done: $SCRIPT_DIR/busybox"
"${CROSS}size" "$SCRIPT_DIR/busybox" 2>/dev/null || true
