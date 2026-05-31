#!/usr/bin/env bash
set -e

DRIVE="${1:-/dev/sda}"
ROOTFS_DIR="$(mktemp -d /tmp/mackerel-root.XXXXXX)"
MOUNT_POINT="$(mktemp -d /tmp/mackerel-mnt.XXXXXX)"

cleanup() {
    if mountpoint -q "$MOUNT_POINT" 2>/dev/null; then
        sudo umount "$MOUNT_POINT"
    fi
    rm -rf "$ROOTFS_DIR"
    rmdir "$MOUNT_POINT" 2>/dev/null || true
}
trap cleanup EXIT

if [ "$(id -u)" -ne 0 ]; then
    echo "This script must be run as root (sudo $0 $*)"
    exit 1
fi

if [ ! -b "$DRIVE" ]; then
    echo "Error: $DRIVE is not a block device"
    exit 1
fi

echo "Warning! This will erase all data on $DRIVE"
read -p "Proceed? (yes/no): " confirm
if [[ "$confirm" != "yes" && "$confirm" != "y" ]]; then
    echo "Exiting."
    exit 1
fi

# ── Partition and format ──────────────────────────────────────────────────────

echo "[*] Partitioning $DRIVE..."
parted --script "$DRIVE" mklabel msdos
parted --script "$DRIVE" mkpart primary fat16 0% 64MB
parted --script "$DRIVE" mkpart primary ext4 64MB 1064MB

echo "[*] Formatting partitions..."
mkfs.fat -F 16 "${DRIVE}1"
mkfs.ext4 "${DRIVE}2"

fdisk -l "$DRIVE"

# ── Build rootfs tree ─────────────────────────────────────────────────────────

echo "[*] Building root filesystem tree..."
mkdir -p "$ROOTFS_DIR"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,boot,root,lib,usr/bin,usr/sbin,usr/lib,usr/share/udhcpc,var/log,var/run,etc/init.d}
chmod 1777 "$ROOTFS_DIR/tmp"
chmod 700  "$ROOTFS_DIR/root"

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

echo "[*] Installing BusyBox..."
if [ ! -f "$SCRIPT_DIR/busybox" ]; then
    echo "Error: busybox binary not found at $SCRIPT_DIR/busybox"
    exit 1
fi
cp "$SCRIPT_DIR/busybox" "$ROOTFS_DIR/bin/busybox"
chmod 755 "$ROOTFS_DIR/bin/busybox"

echo "[*] Creating BusyBox symlinks..."
for cmd in \
    sh ash bash init \
    arp arping awk \
    basename cat chmod chown chgrp clear cp cut \
    date dd df diff dirname dmesg du \
    echo env expr false find free \
    grep gunzip gzip \
    head hexdump hostname \
    id ifconfig insmod ip \
    kill killall ln ls lsmod \
    md5sum mkdir mkfifo mknod more mount mv \
    nc nslookup \
    od \
    ping printf ps pwd \
    readlink realpath rm rmdir rmmod route \
    sed sha1sum sleep sort stat strings stty \
    tail tar tee time touch traceroute traceroute6 tr true \
    rdate syslogd klogd \
    udhcpc uname uniq umount \
    vi \
    wc wget which whoami \
    xargs yes \
    reset sysctl \
    ; do
    ln -sf busybox "$ROOTFS_DIR/bin/$cmd"
done

# init lives in /sbin
ln -sf ../bin/busybox "$ROOTFS_DIR/sbin/init"
ln -sf ../bin/busybox "$ROOTFS_DIR/usr/sbin/udhcpc"

echo "[*] Installing shared libraries..."
REAL_HOME="$(getent passwd "${SUDO_USER:-$(whoami)}" | cut -d: -f6)"
SYSROOT="$REAL_HOME/x-tools/m68k-mackerel-linux-musl/m68k-mackerel-linux-musl/sysroot"
STRIP="$REAL_HOME/x-tools/m68k-mackerel-linux-musl/bin/m68k-mackerel-linux-musl-strip"

if [ ! -d "$SYSROOT" ]; then
    echo "Error: sysroot not found at $SYSROOT"
    exit 1
fi

# musl libc — also serves as the dynamic linker
install -m755 "$SYSROOT/usr/lib/libc.so"        "$ROOTFS_DIR/usr/lib/libc.so"
ln -sf ../usr/lib/libc.so "$ROOTFS_DIR/lib/ld-musl-m68k.so.1"

# GCC runtime
install -m755 "$SYSROOT/lib/libgcc_s.so.2"      "$ROOTFS_DIR/lib/libgcc_s.so.2"
ln -sf libgcc_s.so.2      "$ROOTFS_DIR/lib/libgcc_s.so"

# Atomic operations
install -m755 "$SYSROOT/lib/libatomic.so.1.2.0" "$ROOTFS_DIR/lib/libatomic.so.1.2.0"
ln -sf libatomic.so.1.2.0 "$ROOTFS_DIR/lib/libatomic.so.1"
ln -sf libatomic.so.1.2.0 "$ROOTFS_DIR/lib/libatomic.so"

# Strip debug info from copied libraries
"$STRIP" "$ROOTFS_DIR/usr/lib/libc.so" "$ROOTFS_DIR/lib/libgcc_s.so.2" "$ROOTFS_DIR/lib/libatomic.so.1.2.0"

echo "[*] Writing /usr/share/udhcpc/default.script..."
cat > "$ROOTFS_DIR/usr/share/udhcpc/default.script" <<'EOF'
#!/bin/sh
[ -z "$interface" ] && exit 1
case "$1" in
    deconfig)
        ifconfig "$interface" 0.0.0.0
        ;;
    bound|renew)
        ifconfig "$interface" "$ip" netmask "${subnet:-255.255.255.0}"
        if [ -n "$router" ]; then
            route del default 2>/dev/null || true
            route add default gw "${router%% *}"
        fi
        if [ -n "$dns" ]; then
            printf '' > /etc/resolv.conf
            for d in $dns; do
                printf 'nameserver %s\n' "$d" >> /etc/resolv.conf
            done
        fi
        ;;
esac
EOF
chmod 755 "$ROOTFS_DIR/usr/share/udhcpc/default.script"

echo "[*] Writing /etc/init.d/network..."
cat > "$ROOTFS_DIR/etc/init.d/network" <<'EOF'
#!/bin/sh
LOG=/var/log/network.log
exec >>"$LOG" 2>&1

ifconfig lo 127.0.0.1 up

if ! udhcpc -i eth0 -q -n -t 10 -T 3; then
    echo "network: DHCP failed, skipping time sync"
    exit 0
fi

# One-shot time sync via rdate (RFC 868/TCP)
rdate -s time.nist.gov && echo "network: time synced" || echo "network: time sync failed"
EOF
chmod 755 "$ROOTFS_DIR/etc/init.d/network"

echo "[*] Installing debug tools..."
if [ ! -f "$SCRIPT_DIR/debug/fpu_test" ]; then
    echo "Warning: debug/fpu_test not found — skipping (run make in debug/)"
else
    cp "$SCRIPT_DIR/debug/fpu_test" "$ROOTFS_DIR/usr/bin/fpu_test"
    chmod 755 "$ROOTFS_DIR/usr/bin/fpu_test"
fi

echo "[*] Creating device nodes..."
mknod -m 600 "$ROOTFS_DIR/dev/console" c 5 1
mknod -m 666 "$ROOTFS_DIR/dev/null"    c 1 3
mknod -m 666 "$ROOTFS_DIR/dev/zero"    c 1 5
mknod -m 444 "$ROOTFS_DIR/dev/random"  c 1 8
mknod -m 444 "$ROOTFS_DIR/dev/urandom" c 1 9
mknod -m 660 "$ROOTFS_DIR/dev/ttyXR0" c 4 64
mknod -m 660 "$ROOTFS_DIR/dev/ttyXR1" c 4 65

echo "[*] Writing /etc/sysctl.conf..."
cat > "$ROOTFS_DIR/etc/sysctl.conf" <<'EOF'
net.ipv4.ping_group_range = 0 2147483647
kernel.printk = 3 4 1 3
EOF

echo "[*] Writing /etc/inittab..."
cat > "$ROOTFS_DIR/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t msdos -o ro /dev/sda1 /boot
::sysinit:/bin/sysctl -p /etc/sysctl.conf
::sysinit:/bin/syslogd
::sysinit:/bin/klogd
::once:/etc/init.d/network
::respawn:/etc/login <>/dev/ttyXR0 >/dev/ttyXR0 2>&1
EOF

cat > "$ROOTFS_DIR/etc/login" <<'EOF'
#!/bin/sh
export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd "$HOME"
exec /bin/sh
EOF
chmod 755 "$ROOTFS_DIR/etc/login"

echo "[*] Writing /etc/fstab..."
cat > "$ROOTFS_DIR/etc/fstab" <<'EOF'
/dev/sda1   /boot   msdos   ro,noatime          0 0
/dev/sda2   /       ext4    defaults,noatime    0 1
proc        /proc   proc    defaults            0 0
sysfs       /sys    sysfs   defaults            0 0
devtmpfs    /dev    devtmpfs defaults           0 0
tmpfs       /tmp    tmpfs   defaults            0 0
EOF

echo "[*] Writing /etc/profile..."
cat > "$ROOTFS_DIR/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd "$HOME"
EOF

echo "[*] Writing /etc/passwd..."
echo "root:x:0:0:root:/root:/bin/sh" > "$ROOTFS_DIR/etc/passwd"

echo "[*] Writing /etc/hostname..."
echo "mackerel" > "$ROOTFS_DIR/etc/hostname"

echo "[*] Copying kernel image to ${DRIVE}1..."
if [ ! -f "$SCRIPT_DIR/image.bin" ]; then
    echo "Warning: image.bin not found at $SCRIPT_DIR/image.bin — skipping boot partition"
else
    mount "${DRIVE}1" "$MOUNT_POINT"
    cp "$SCRIPT_DIR/image.bin" "$MOUNT_POINT/IMAGE.BIN"
    sync
    umount "$MOUNT_POINT"
fi

echo "[*] Mounting ${DRIVE}2 at $MOUNT_POINT..."
mount "${DRIVE}2" "$MOUNT_POINT"

echo "[*] Copying rootfs to ${DRIVE}2..."
cp -a "$ROOTFS_DIR/." "$MOUNT_POINT/"

echo "[*] Syncing..."
sync

echo "[*] Unmounting ${DRIVE}2..."
umount "$MOUNT_POINT"

echo
echo "[+] Done."
echo "    Boot partition: ${DRIVE}1 (FAT16, 64 MB) — IMAGE.BIN"
echo "    Root partition: ${DRIVE}2 (ext4, 1 GB)  — rootfs"
