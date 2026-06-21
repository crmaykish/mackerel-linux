#!/usr/bin/env bash
# Build the root filesystem for a Mackerel board.
# bash build_rootfs.sh [board]       board: 30 (default), 10, or 08
# Note: depends on build_busybox.sh having been run for the same board
set -e

BOARD="${1:-30}"
SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"

case "$BOARD" in
    30) BUSYBOX="$SCRIPT_DIR/busybox"            ; STAGE="$SCRIPT_DIR/rootfs_mackerel30" ;;
    10) BUSYBOX="$SCRIPT_DIR/busybox_nommu"      ; STAGE="$SCRIPT_DIR/initramfs"         ;;
    08) BUSYBOX="$SCRIPT_DIR/busybox_mackerel08" ; STAGE="$SCRIPT_DIR/romfs_mackerel08"  ;;
    f|F)  BUSYBOX="$SCRIPT_DIR/busybox_mackerelf"  ; STAGE="$SCRIPT_DIR/romfs_mackerelf"   ;;
    *)  echo "Usage: $0 [board]   (board: 30, 10, 08, or F; default 30)"; exit 1 ;;
esac

if [ ! -f "$BUSYBOX" ]; then
    echo "ERROR: $BUSYBOX not found. Run: bash build_busybox.sh $BOARD"
    exit 1
fi

# Mackerel-30
build_rootfs_30() {
    echo "Building Mackerel-30 root filesystem tree at $STAGE..."
    rm -rf "$STAGE"
    mkdir -p "$STAGE"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,boot,root,lib,usr/bin,usr/sbin,usr/lib,usr/share/udhcpc,var/log,var/run,etc/init.d}
    chmod 1777 "$STAGE/tmp"
    chmod 700  "$STAGE/root"

    echo "Installing BusyBox..."
    cp "$BUSYBOX" "$STAGE/bin/busybox"
    chmod 755 "$STAGE/bin/busybox"

    echo "Creating BusyBox symlinks..."
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
        ln -sf busybox "$STAGE/bin/$cmd"
    done

    # init lives in /sbin
    ln -sf ../bin/busybox "$STAGE/sbin/init"
    ln -sf ../bin/busybox "$STAGE/usr/sbin/udhcpc"

    echo "Installing shared libraries..."
    SYSROOT="$HOME/x-tools/m68k-mackerel-linux-musl/m68k-mackerel-linux-musl/sysroot"
    STRIP="$HOME/x-tools/m68k-mackerel-linux-musl/bin/m68k-mackerel-linux-musl-strip"

    if [ ! -d "$SYSROOT" ]; then
        echo "Error: sysroot not found at $SYSROOT"
        exit 1
    fi

    # musl libc — also serves as the dynamic linker
    install -m755 "$SYSROOT/usr/lib/libc.so"        "$STAGE/usr/lib/libc.so"
    ln -sf ../usr/lib/libc.so "$STAGE/lib/ld-musl-m68k.so.1"

    # GCC runtime
    install -m755 "$SYSROOT/lib/libgcc_s.so.2"      "$STAGE/lib/libgcc_s.so.2"
    ln -sf libgcc_s.so.2      "$STAGE/lib/libgcc_s.so"

    # Atomic operations
    install -m755 "$SYSROOT/lib/libatomic.so.1.2.0" "$STAGE/lib/libatomic.so.1.2.0"
    ln -sf libatomic.so.1.2.0 "$STAGE/lib/libatomic.so.1"
    ln -sf libatomic.so.1.2.0 "$STAGE/lib/libatomic.so"

    # Strip debug info from copied libraries
    "$STRIP" "$STAGE/usr/lib/libc.so" "$STAGE/lib/libgcc_s.so.2" "$STAGE/lib/libatomic.so.1.2.0"

    echo "Writing /usr/share/udhcpc/default.script..."
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

    echo "Writing /etc/init.d/network..."
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

    echo "Installing debug tools..."
    if [ ! -f "$SCRIPT_DIR/debug/fpu_test" ]; then
        echo "Warning: debug/fpu_test not found — skipping (run make in debug/)"
    else
        cp "$SCRIPT_DIR/debug/fpu_test" "$STAGE/usr/bin/fpu_test"
        chmod 755 "$STAGE/usr/bin/fpu_test"
    fi

    echo "Writing /etc/sysctl.conf..."
    cat > "$STAGE/etc/sysctl.conf" <<'EOF'
net.ipv4.ping_group_range = 0 2147483647
kernel.printk = 3 4 1 3
EOF

    echo "Writing /etc/inittab..."
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

    echo "Writing /etc/fstab..."
    cat > "$STAGE/etc/fstab" <<'EOF'
/dev/sda1   /boot   msdos   ro,noatime          0 0
/dev/sda2   /       ext4    defaults,noatime    0 1
proc        /proc   proc    defaults            0 0
sysfs       /sys    sysfs   defaults            0 0
devtmpfs    /dev    devtmpfs defaults           0 0
tmpfs       /tmp    tmpfs   defaults            0 0
EOF

    echo "Writing /etc/profile..."
    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
cd "$HOME"
EOF

    echo "Writing /etc/passwd..."
    echo "root:x:0:0:root:/root:/bin/sh" > "$STAGE/etc/passwd"

    echo "Writing /etc/hostname..."
    echo "mackerel" > "$STAGE/etc/hostname"

    echo
    echo "[+] Done: $STAGE/  (copy to ext4 with install_disk.sh)"
}

# Mackerel-10
build_rootfs_10() {
    local LIST_FILE="$SCRIPT_DIR/initramfs.list"

    echo "Creating initramfs staging directory at $STAGE..."
    rm -rf "$STAGE"
    mkdir -p "$STAGE"/{bin,sbin,etc,proc,sys,dev,tmp,root}

    echo "Installing busybox..."
    cp "$BUSYBOX" "$STAGE/bin/busybox"
    chmod 755 "$STAGE/bin/busybox"

    echo "Creating busybox symlinks..."
    for cmd in \
        sh hush \
        echo cat ls mkdir rm cp mv ln touch pwd \
        sleep ps kill killall \
        mount umount \
        hostname uname env \
        dmesg \
        grep sed cut tr wc head tail sort uniq \
        find xargs \
        expr test printf date \
        free df stat readlink basename dirname \
        dd clear reset \
        ; do
        ln -sf busybox "$STAGE/bin/$cmd"
    done
    ln -sf ../bin/busybox "$STAGE/sbin/init"

    echo "Writing /etc/inittab..."
    cat > "$STAGE/etc/inittab" <<'EOF'
# Mackerel-10 inittab
::sysinit:/bin/mount -t devtmpfs dev /dev
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/mount -t sysfs sysfs /sys
::sysinit:/bin/hostname mackerel
::respawn:-/bin/sh
::restart:/sbin/init
::ctrlaltdel:/bin/reboot
EOF

    echo "Writing /etc/passwd..."
    echo "root::0:0:root:/root:/bin/sh" > "$STAGE/etc/passwd"

    echo "Writing /etc/profile..."
    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin
export PS1='\u@mackerel:\w\$ '
cd "$HOME"
EOF

    echo "Generating initramfs.list"
    cat > "$LIST_FILE" <<EOF
# Mackerel-10 initramfs

dir  /proc          0755 0 0
dir  /sys           0755 0 0
dir  /dev           0755 0 0
dir  /bin           0755 0 0
dir  /sbin          0755 0 0
dir  /etc           0755 0 0
dir  /tmp           0777 0 0
dir  /root          0700 0 0

# Console device node
nod  /dev/console   0600 0 0 c 5 1

# BusyBox binary
file /bin/busybox   ${STAGE}/bin/busybox 0755 0 0

slink /init         /bin/busybox 0755 0 0

# BusyBox symlinks
slink /sbin/init         /bin/busybox 0755 0 0
slink /bin/sh            busybox 0755 0 0
slink /bin/hush          busybox 0755 0 0
slink /bin/echo          busybox 0755 0 0
slink /bin/cat           busybox 0755 0 0
slink /bin/ls            busybox 0755 0 0
slink /bin/mkdir         busybox 0755 0 0
slink /bin/rm            busybox 0755 0 0
slink /bin/cp            busybox 0755 0 0
slink /bin/mv            busybox 0755 0 0
slink /bin/ln            busybox 0755 0 0
slink /bin/touch         busybox 0755 0 0
slink /bin/pwd           busybox 0755 0 0
slink /bin/sleep         busybox 0755 0 0
slink /bin/ps            busybox 0755 0 0
slink /bin/kill          busybox 0755 0 0
slink /bin/killall       busybox 0755 0 0
slink /bin/mount         busybox 0755 0 0
slink /bin/umount        busybox 0755 0 0
slink /bin/hostname      busybox 0755 0 0
slink /bin/uname         busybox 0755 0 0
slink /bin/dmesg         busybox 0755 0 0
slink /bin/grep          busybox 0755 0 0
slink /bin/sed           busybox 0755 0 0
slink /bin/find          busybox 0755 0 0
slink /bin/date          busybox 0755 0 0
slink /bin/free          busybox 0755 0 0
slink /bin/df            busybox 0755 0 0
slink /bin/env           busybox 0755 0 0
slink /bin/dd            busybox 0755 0 0
slink /bin/clear         busybox 0755 0 0
slink /bin/reset         busybox 0755 0 0

# Config files
file /etc/inittab        ${STAGE}/etc/inittab  0644 0 0
file /etc/passwd         ${STAGE}/etc/passwd   0644 0 0
file /etc/profile        ${STAGE}/etc/profile  0644 0 0
EOF

    echo "Done!"
}

# Mackerel-08
build_rootfs_08() {
    local OUT="$SCRIPT_DIR/romfs.img"

    echo "Staging rootfs at $STAGE..."
    rm -rf "$STAGE"
    mkdir -p "$STAGE"/{bin,sbin,etc,proc,root,dev}

    cp "$BUSYBOX" "$STAGE/bin/busybox"; chmod 755 "$STAGE/bin/busybox"

    for cmd in sh echo cat ls mkdir rm rmdir cp mv ln pwd mount umount ps kill \
               uname dmesg sleep free clear true false test halt poweroff reboot \
               grep egrep fgrep sed find xargs dd \
               head tail wc sort cut tr date df touch printf \
               chmod chown du hexdump od strings top mknod mkfifo; do
        ln -sf busybox "$STAGE/bin/$cmd"
    done

    ln -sf /bin/busybox "$STAGE/init"
    ln -sf /bin/busybox "$STAGE/sbin/init"

    cat > "$STAGE/etc/inittab" <<'EOF'
::sysinit:/bin/mount -t proc proc /proc
::sysinit:/bin/echo Mackerel-08 userspace up
::respawn:-/bin/sh
::ctrlaltdel:/bin/reboot
EOF

    cat > "$STAGE/etc/profile" <<'EOF'
export HOME=/root
export PATH=/bin:/sbin
export PS1='mackerel:\w# '
EOF

    echo "Building romfs image..."
    genromfs -d "$STAGE" -f "$OUT" -V 'mackerel08'

    assemble_rom08 "$OUT"

    echo "Done!"
}

# Combine the bootloader.bin and ROMfs image into a single bin file for flashing
assemble_rom08() {
    local ROMFS="$1"
    local FW_DIR="${SCRIPT_DIR}/../mackerel-68k/firmware"
    local BL="$FW_DIR/bootloader.bin"
    local ROM_SIZE=524288 # 512K
    local OUT="$SCRIPT_DIR/rom08.bin"

    if [ ! -f "$FW_DIR/bootloader.bin" ]; then
        echo "ERROR: $FW_DIR/bootloader.bin not found. Build the Mackerel-08 bootloader first..."
        exit 1
    fi

    echo "Combining bootloader and ROMfs..."
    dd if=/dev/zero of="$OUT" bs=4096 count=$((ROM_SIZE / 4096)) status=none
    dd if="$BL" of="$OUT" conv=notrunc bs=4096 status=none
    dd if="$ROMFS" of="$OUT" conv=notrunc bs=4096 seek=16 status=none
    
    echo "Flash $OUT with minipro."
}

# romf.bin, loaded to SDRAM by the bootloader
build_rootfs_f() {
    local OUT="$SCRIPT_DIR/romf.bin"

    echo "Staging Mackerel-F ROMfs tree at $STAGE..."
    rm -rf "$STAGE"
    mkdir -p "$STAGE"/{bin,sbin,etc,proc,sys,dev,tmp,mnt,root}

    cp "$BUSYBOX" "$STAGE/bin/busybox"; chmod 755 "$STAGE/bin/busybox"

    for cmd in \
        sh hush echo cat ls mkdir rm rmdir cp mv ln touch pwd sync \
        chmod chown mknod dd mount umount clear \
        ps kill sleep dmesg uname hostname uptime free df true false test grep sed \
        reboot halt poweroff \
        ifconfig ping route udhcpc wget telnetd; do
        ln -sf busybox "$STAGE/bin/$cmd"
    done

    ln -sf /bin/busybox "$STAGE/init"
    ln -sf /bin/busybox "$STAGE/sbin/init"

    # /dev is auto-populated by CONFIG_DEVTMPFS_MOUNT; /proc /sys /tmp via inittab.
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
::respawn:/bin/telnetd -F -l /bin/sh
::respawn:-/bin/sh
::ctrlaltdel:/bin/reboot
EOF

    # The microSD's first mmc_spi probe times out right after the bootloader's
    # The SD card does not realiably probe the first time. This is a hack to keep
    # retrying in the background until it works...
    mkdir -p "$STAGE/etc/init.d"
    cat > "$STAGE/etc/init.d/sdcard" <<'EOF'
#!/bin/sh
(
    sleep 10
    i=0
    while [ ! -e /dev/mmcblk0 ] && [ "$i" -lt 8 ]; do
        echo spi0.0 > /sys/bus/spi/drivers/mmc_spi/unbind 2>/dev/null
        echo spi0.0 > /sys/bus/spi/drivers/mmc_spi/bind 2>/dev/null
        [ -e /dev/mmcblk0 ] && break
        sleep 5
        i=$((i + 1))
    done
) &
EOF
    chmod 755 "$STAGE/etc/init.d/sdcard"

    # DHCP setup on boot
    mkdir -p "$STAGE/usr/share/udhcpc"
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

    echo "Building ROMfs image -> $OUT ..."
    genromfs -d "$STAGE" -f "$OUT" -V 'mackerelf'

    local sz
    sz=$(stat -c%s "$OUT")
    echo "[+] $OUT  ($sz bytes)"
    if [ "$sz" -gt $((0xc0000)) ]; then
        echo "WARNING: romf.bin ($sz) exceeds the 768 KB (0xC0000) region at 0x700000!"
        echo "         Trim busybox applets or the region overlaps the bootloader RAM."
    fi
    echo "Copy $OUT to the SD card's FAT16 partition as romf.bin."
}

if [ "$BOARD" = "f" ] || [ "$BOARD" = "F" ]; then
    build_rootfs_f
else
    # Otherwise, just call the rootfs generator for the specified board
    build_rootfs_"${BOARD}"
fi
