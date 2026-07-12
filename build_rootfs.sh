#!/usr/bin/env bash
# Build the root filesystem for a Mackerel board.
# bash build_rootfs.sh [board]      board: 30 (default), 10, 08, or f
# Depends on build_busybox.sh having been run for the same board
set -e

BOARD="${1:-30}"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

case "$BOARD" in
    30)  BB=busybox            ; STAGE=rootfs_mackerel30 ; FORMAT=dir       ;;
    10)  BB=busybox_nommu      ; STAGE=romfs_mackerel10  ; FORMAT=romfs     ;;
    08)  BB=busybox_mackerel08 ; STAGE=romfs_mackerel08  ; FORMAT=romfs-rom ;;
    f|F) BB=busybox_mackerelf  ; STAGE=romfs_mackerelf   ; FORMAT=romfs     ; BOARD=f ;;
    *)   echo "Usage: $0 [board]   (board: 30, 10, 08, or f; default 30)"; exit 1 ;;
esac
BUSYBOX="$SCRIPT_DIR/$BB"; STAGE="$SCRIPT_DIR/$STAGE"; LINKS="$BUSYBOX.links"

[ -f "$BUSYBOX" ] || { echo "ERROR: $BUSYBOX not found. Run: bash build_busybox.sh $BOARD"; exit 1; }
[ -f "$LINKS" ]   || { echo "ERROR: $LINKS not found. Run: bash build_busybox.sh $BOARD"; exit 1; }

stage_busybox() {
    echo "Staging $STAGE (busybox + applet symlinks from $(basename "$LINKS"))..."
    rm -rf "$STAGE"
    mkdir -p "$STAGE/bin"
    cp "$BUSYBOX" "$STAGE/bin/busybox"; chmod 755 "$STAGE/bin/busybox"
    while read -r p; do
        case "$p" in ""|/bin/busybox) continue ;; esac
        mkdir -p "$STAGE${p%/*}"
        ln -sf /bin/busybox "$STAGE$p"
    done < "$LINKS"
    mkdir -p "$STAGE/sbin"; ln -sf /bin/busybox "$STAGE/sbin/init"
    [ "$FORMAT" = dir ] || ln -sf /bin/busybox "$STAGE/init"
}

# Mackerel-30: ext4 disk root (musl, dynamic libs)
config_30() {
    mkdir -p "$STAGE"/{etc,proc,sys,dev,tmp,mnt,boot,root,lib,usr/bin,usr/lib,usr/share/udhcpc,var/log,var/run,etc/init.d}
    chmod 1777 "$STAGE/tmp"; chmod 700 "$STAGE/root"

    echo "Installing shared libraries..."
    local SYSROOT="$HOME/x-tools/m68k-mackerel-linux-musl/m68k-mackerel-linux-musl/sysroot"
    local STRIP="$HOME/x-tools/m68k-mackerel-linux-musl/bin/m68k-mackerel-linux-musl-strip"
    [ -d "$SYSROOT" ] || { echo "Error: sysroot not found at $SYSROOT"; exit 1; }
    install -m755 "$SYSROOT/usr/lib/libc.so"        "$STAGE/usr/lib/libc.so"
    ln -sf ../usr/lib/libc.so "$STAGE/lib/ld-musl-m68k.so.1"
    install -m755 "$SYSROOT/lib/libgcc_s.so.2"      "$STAGE/lib/libgcc_s.so.2"
    ln -sf libgcc_s.so.2      "$STAGE/lib/libgcc_s.so"
    install -m755 "$SYSROOT/lib/libatomic.so.1.2.0" "$STAGE/lib/libatomic.so.1.2.0"
    ln -sf libatomic.so.1.2.0 "$STAGE/lib/libatomic.so.1"
    ln -sf libatomic.so.1.2.0 "$STAGE/lib/libatomic.so"
    "$STRIP" "$STAGE/usr/lib/libc.so" "$STAGE/lib/libgcc_s.so.2" "$STAGE/lib/libatomic.so.1.2.0"

    cat > "$STAGE/usr/share/udhcpc/default.script" <<'EOF'
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
    chmod 755 "$STAGE/usr/share/udhcpc/default.script"

    cat > "$STAGE/etc/init.d/network" <<'EOF'
#!/bin/sh
LOG=/var/log/network.log
exec >>"$LOG" 2>&1

ifconfig lo 127.0.0.1 up

if ! udhcpc -i eth0 -q -n -t 10 -T 3; then
    echo "network: DHCP failed, skipping time sync"
    exit 0
fi

rdate -s time.nist.gov && echo "network: time synced" || echo "network: time sync failed"
EOF
    chmod 755 "$STAGE/etc/init.d/network"

    if [ -f "$SCRIPT_DIR/debug/fpu_test" ]; then
        cp "$SCRIPT_DIR/debug/fpu_test" "$STAGE/usr/bin/fpu_test"; chmod 755 "$STAGE/usr/bin/fpu_test"
    else
        echo "Warning: debug/fpu_test not found — skipping (run make in debug/)"
    fi

    cat > "$STAGE/etc/sysctl.conf" <<'EOF'
net.ipv4.ping_group_range = 0 2147483647
kernel.printk = 3 4 1 3
EOF

    cat > "$STAGE/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t msdos -o ro /dev/sda1 /boot
::sysinit:/bin/sysctl -p /etc/sysctl.conf
::sysinit:/bin/syslogd
::sysinit:/bin/klogd
::once:/etc/init.d/network
::respawn:/etc/login <>/dev/ttyXR0 >/dev/ttyXR0 2>&1
EOF

    cat > "$STAGE/etc/login" <<'EOF'
#!/bin/sh
export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd "$HOME"
exec /bin/sh
EOF
    chmod 755 "$STAGE/etc/login"

    cat > "$STAGE/etc/fstab" <<'EOF'
/dev/sda1   /boot   msdos   ro,noatime          0 0
/dev/sda2   /       ext4    defaults,noatime    0 1
proc        /proc   proc    defaults            0 0
sysfs       /sys    sysfs   defaults            0 0
devtmpfs    /dev    devtmpfs defaults           0 0
tmpfs       /tmp    tmpfs   defaults            0 0
EOF

    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd "$HOME"
EOF

    echo "root:x:0:0:root:/root:/bin/sh" > "$STAGE/etc/passwd"
    echo "mackerel" > "$STAGE/etc/hostname"
}

# Mackerel-10: ROMfs root, W5500 networking, telnetd
config_10() {
    mkdir -p "$STAGE"/{etc,etc/init.d,proc,sys,dev,tmp,root,usr/share/udhcpc}

    cat > "$STAGE/etc/inittab" <<'EOF'
# Mackerel-10 inittab (read-only ROMfs root)
::sysinit:/bin/mount -t devtmpfs dev /dev
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t tmpfs tmpfs /tmp
::sysinit:/bin/mkdir -p /dev/pts
::sysinit:/bin/mount -t devpts devpts /dev/pts
::sysinit:/bin/hostname mackerel
::once:/etc/init.d/network
::respawn:/usr/sbin/telnetd -F -l /bin/sh
::respawn:-/bin/sh
::restart:/sbin/init
::ctrlaltdel:/sbin/reboot
EOF

    echo "root::0:0:root:/root:/bin/sh" > "$STAGE/etc/passwd"

    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin
export PS1='\u@mackerel:\w\$ '
cd "$HOME"
EOF

    ln -sf /tmp/resolv.conf "$STAGE/etc/resolv.conf"

    cat > "$STAGE/usr/share/udhcpc/default.script" <<'EOF'
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
            : > /etc/resolv.conf
            for d in $dns; do
                printf 'nameserver %s\n' "$d" >> /etc/resolv.conf
            done
        fi
        ;;
esac
EOF
    chmod 755 "$STAGE/usr/share/udhcpc/default.script"

    cat > "$STAGE/etc/init.d/network" <<'EOF'
#!/bin/sh
# DHCP on eth0 (W5500) with a FIXED MAC (matches the bootloader's netboot MAC,
# firmware/netboot.c) so the router can hand out the same IP every boot. Logs to
# /tmp because the ROMfs root is read-only.
exec >>/tmp/network.log 2>&1
ifconfig lo 127.0.0.1 up
ifconfig eth0 down
ifconfig eth0 hw ether 02:4d:4b:52:46:01
ifconfig eth0 up
udhcpc -i eth0 -q -n -t 10 -T 3 || echo "network: DHCP failed"
EOF
    chmod 755 "$STAGE/etc/init.d/network"
}

# Mackerel-08: minimal ROMfs root (in the boot ROM), no networking.
config_08() {
    mkdir -p "$STAGE"/{etc,proc,root,dev}

    cat > "$STAGE/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/echo Mackerel-08 userspace up
::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
EOF

    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin
export PS1='mackerel:\w# '
EOF
}

# Mackerel-F: ROMfs root, microSD + W5500
config_f() {
    mkdir -p "$STAGE"/{etc,etc/init.d,proc,sys,dev,tmp,mnt,root,usr/share/udhcpc}

    cat > "$STAGE/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/mount -t tmpfs tmpfs /tmp
::sysinit:/bin/mkdir -p /dev/pts
::sysinit:/bin/mount -t devpts devpts /dev/pts
::sysinit:/bin/hostname mackerel-f
::sysinit:/etc/init.d/sdcard
::sysinit:/etc/init.d/network
::sysinit:/bin/echo Mackerel-F uClinux - init OK
::respawn:/usr/sbin/telnetd -F -l /bin/sh
::respawn:-/bin/sh
::ctrlaltdel:/sbin/reboot
EOF

    # Block boot until the microSD is up, then mount its Linux partition on /root.
    cat > "$STAGE/etc/init.d/sdcard" <<'EOF'
#!/bin/sh
echo "Waiting for SD card..."
sleep 10
i=0
while [ ! -e /dev/mmcblk0 ]; do
    echo spi0.0 > /sys/bus/spi/drivers/mmc_spi/unbind 2>/dev/null
    echo spi0.0 > /sys/bus/spi/drivers/mmc_spi/bind 2>/dev/null
    sleep 5
    i=$((i + 1))
    if [ "$i" -ge 24 ]; then
        echo "SD card did not appear after ~$((10 + i * 5))s; /root NOT mounted"
        exit 0
    fi
done
# the block device is up; give the partition scan a moment to create p2
[ -e /dev/mmcblk0p2 ] || sleep 2
if mount /dev/mmcblk0p2 /root; then
    echo "Mounted /dev/mmcblk0p2 on /root"
else
    echo "SD up but mounting /dev/mmcblk0p2 on /root FAILED"
fi
EOF
    chmod 755 "$STAGE/etc/init.d/sdcard"

    cat > "$STAGE/usr/share/udhcpc/default.script" <<'EOF'
#!/bin/sh
[ -z "$interface" ] && exit 1
case "$1" in
    deconfig)
        ifconfig "$interface" 0.0.0.0
        ;;
    bound|renew)
        ifconfig "$interface" "$ip" netmask "${subnet:-255.255.255.0}"
        if [ -n "$router" ]; then
            route del default 2>/dev/null
            route add default gw "${router%% *}"
        fi
        : > /etc/resolv.conf
        for d in $dns; do echo "nameserver $d" >> /etc/resolv.conf; done
        ;;
esac
EOF
    chmod 755 "$STAGE/usr/share/udhcpc/default.script"
    ln -sf /tmp/resolv.conf "$STAGE/etc/resolv.conf"

    cat > "$STAGE/etc/init.d/network" <<'EOF'
#!/bin/sh
ifconfig lo 127.0.0.1 up
(
    # Fixed MAC (matches the bootloader's W5500 MAC)
    ifconfig eth0 hw ether 02:4D:4B:52:46:01
    ifconfig eth0 0.0.0.0 up
    i=0
    while [ "$(cat /sys/class/net/eth0/carrier 2>/dev/null)" != "1" ] && [ "$i" -lt 20 ]; do
        sleep 1
        i=$((i + 1))
    done
    udhcpc -i eth0 -q -t 15 -T 3 -p /tmp/udhcpc.eth0.pid >/dev/null 2>&1
) &
EOF
    chmod 755 "$STAGE/etc/init.d/network"

    echo "root::0:0:root:/root:/bin/sh" > "$STAGE/etc/passwd"

    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin
export PS1='\u@mackerel-f:\w\$ '
cd "$HOME"
EOF
}

# Combine bootloader.bin + ROMfs into a single 512 KB flash image (Mackerel-08).
assemble_rom08() {
    local ROMFS="$1"
    local FW_DIR="${SCRIPT_DIR}/../mackerel-68k/firmware"
    local BL="$FW_DIR/bootloader.bin"
    local ROM_SIZE=524288 # 512K
    local OUT="$SCRIPT_DIR/rom08.bin"

    [ -f "$BL" ] || { echo "ERROR: $BL not found. Build the Mackerel-08 bootloader first..."; exit 1; }

    echo "Combining bootloader and ROMfs..."
    dd if=/dev/zero of="$OUT" bs=4096 count=$((ROM_SIZE / 4096)) status=none
    dd if="$BL"    of="$OUT" conv=notrunc bs=4096 status=none
    dd if="$ROMFS" of="$OUT" conv=notrunc bs=4096 seek=16 status=none
    echo "Flash $OUT with minipro."
}

package() {
    case "$FORMAT" in
        dir)
            echo "$STAGE/  (copy to ext4 with install_disk.sh)"
            ;;
        romfs)
            # rom.bin is appended into image.bin by build_kernel.sh (MTD_UCLINUX).
            local OUT="$SCRIPT_DIR/rom.bin"
            genromfs -d "$STAGE" -f "$OUT" -V "mackerel$BOARD"
            echo "$OUT  ($(stat -c%s "$OUT") bytes) -> appended into image.bin"
            ;;
        romfs-rom)
            # Mackerel-08: ROMfs lives in the boot ROM, combined with the bootloader.
            local OUT="$SCRIPT_DIR/romfs.img"
            genromfs -d "$STAGE" -f "$OUT" -V "mackerel08"
            assemble_rom08 "$OUT"
            ;;
    esac
}

stage_busybox
config_"$BOARD"
package
