#!/usr/bin/env bash
# Build a static bFLT BusyBox binary for m68k NOMMU (Mackerel-10).
#
# Strategy: defconfig sets ALL options (silencing silentoldconfig), then we
# aggressively disable everything except ~25 core applets. This keeps the
# binary small enough to avoid m68k GOT16O overflow with -msep-data.
#
# Uses hush shell (ash explicitly refuses NOMMU builds).
# Output: ./busybox_nommu in the repo root.
set -e

BUSYBOX_VERSION=1.36.1
BUSYBOX_URL="https://busybox.net/downloads/busybox-${BUSYBOX_VERSION}.tar.bz2"

SCRIPT_DIR="$(dirname "$(readlink -f "$0")")"
BUILD_DIR="$SCRIPT_DIR/.busybox-nommu-build"
# Toolchain is overridable: CROSS=<tuple>- TOOLCHAIN_BIN=<path> ./build_busybox_nommu.sh
CROSS=${CROSS:-m68k-mackerel-uclinux-uclibc-}
TOOLCHAIN_BIN=${TOOLCHAIN_BIN:-~/x-tools/m68k-mackerel-uclinux-uclibc/bin}

export PATH=$PATH:${TOOLCHAIN_BIN}

# Startup: stock crt1.o + __uClibc_main. (The old "tail hang" that once forced a
# custom crt0 was actually the non-PIC libgcc bug, fixed by building libgcc
# -msep-data via CT_TARGET_CFLAGS in the toolchain.)

# ── helpers ──────────────────────────────────────────────────────────────────
en()  {
    local o="$1"
    sed -i "s|^# CONFIG_${o} is not set\$|CONFIG_${o}=y|; \
            s|^CONFIG_${o}=.*\$|CONFIG_${o}=y|" .config
    grep -q "CONFIG_${o}" .config || echo "CONFIG_${o}=y" >> .config
}
dis() {
    local o="$1"
    sed -i "s|^CONFIG_${o}=.*\$|# CONFIG_${o} is not set|" .config
    grep -q "CONFIG_${o}" .config || echo "# CONFIG_${o} is not set" >> .config
}
str() {
    local o="$1" v="$2"
    sed -i "s|^CONFIG_${o}=.*\$|CONFIG_${o}=\"${v}\"|" .config
    grep -q "CONFIG_${o}" .config || echo "CONFIG_${o}=\"${v}\"" >> .config
}

# ─────────────────────────────────────────────────────────────────────────────

# Cache the tarball outside the build dir so rebuilds don't re-download
# (busybox.net can be very slow). -c resumes a partial download.
CACHE_DIR="$HOME/src"
TARBALL="busybox-${BUSYBOX_VERSION}.tar.bz2"
mkdir -p "$CACHE_DIR"
if ! bzip2 -t "$CACHE_DIR/$TARBALL" 2>/dev/null; then
    echo "[*] Downloading BusyBox ${BUSYBOX_VERSION} to $CACHE_DIR ..."
    wget -c -q --show-progress -O "$CACHE_DIR/$TARBALL" "$BUSYBOX_URL"
fi

echo "[*] Preparing build directory..."
rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"
cp "$CACHE_DIR/$TARBALL" .
tar xf "$TARBALL"
cd "busybox-${BUSYBOX_VERSION}"

echo "[*] Starting from defconfig (all options answered, no interactive prompts)..."
make ARCH=m68k CROSS_COMPILE="$CROSS" defconfig

echo "[*] Applying NOMMU bFLT build settings..."
str CROSS_COMPILER_PREFIX "${CROSS}"
str EXTRA_CFLAGS "-msep-data -fno-common -m68000"
str EXTRA_LDFLAGS "-Wl,-elf2flt"
str EXTRA_LDLIBS  ""
en  STATIC
dis STATIC_LIBGCC
dis DESKTOP
dis EXTRA_COMPAT
dis FEDORA_COMPAT

echo "[*] Selecting hush shell (ash refuses to build on NOMMU)..."
# Disable ash and every sub-option
for o in ASH ASH_OPTIMIZE_FOR_SIZE ASH_INTERNAL_GLOB ASH_ALIAS \
         ASH_BASH_COMPAT ASH_JOB_CONTROL ASH_ECHO ASH_PRINTF \
         ASH_TEST ASH_HELP ASH_GETOPTS ASH_CMDCMD ASH_MAIL \
         ASH_EXPAND_PRETYPE_PUSH ASH_RANDOM_SUPPORT ASH_IDLE_TIMEOUT; do
    dis "$o"
done
en  HUSH
en  HUSH_INTERACTIVE
en  HUSH_ECHO
en  HUSH_PRINTF
en  HUSH_TEST
en  HUSH_HELP
en  HUSH_EXPORT_N
en  HUSH_BRACE_EXPANSION
en  HUSH_BASH_COMPAT
# SH_IS_* is a Kconfig 'choice'. Defconfig selects SH_IS_ASH; with ASH
# disabled we must replace the selection entirely or silentoldconfig re-asks.
sed -i '/^CONFIG_SH_IS_/d'                .config
sed -i '/^# CONFIG_SH_IS_/d'              .config
echo "CONFIG_SH_IS_HUSH=y"              >> .config
echo "# CONFIG_SH_IS_ASH is not set"   >> .config
echo "# CONFIG_SH_IS_NONE is not set"  >> .config
dis BASH_IS_ASH
dis BASH_IS_HUSH
en  FEATURE_SH_STANDALONE
en  FEATURE_SH_NOFORK

# Disable job control: on this minimal NOMMU system the session/controlling-tty/
# process-group setup is incomplete, so hush's job control (tcsetpgrp / WUNTRACED
# / foreground-pgrp reclaim) deadlocks AFTER a fork+exec'd foreground command
# (the child is reaped fine, then hush hangs reclaiming the terminal). A console
# shell here doesn't need job control.
dis HUSH_JOB

# Disable line editing: its raw-mode tcsetattr + char-by-char reader leaves the
# interactive shell silent on this uart (cat/cooked I/O and `sh -c` both work).
# Cooked-mode line input (like cat) gives a working interactive prompt.
dis FEATURE_EDITING
dis FEATURE_EDITING_VI
dis FEATURE_EDITING_HISTORY
dis FEATURE_EDITING_SAVEHISTORY
dis FEATURE_EDITING_SAVE_ON_EXIT
dis FEATURE_TAB_COMPLETION
dis FEATURE_USERNAME_COMPLETION
dis FEATURE_EDITING_FANCY_PROMPT
dis FEATURE_EDITING_ASK_TERMINAL

echo "[*] Disabling networking (no driver on Mackerel-10)..."
for o in UDHCPC UDHCPC6 UDHCPD DNSD HTTPD FTPGET FTPPUT \
         WGET NC NETCAT PING PING6 TELNET TELNETD \
         IFCONFIG IFUP IFDOWN ROUTE ARP ARPING RDATE \
         TRACEROUTE TRACEROUTE6 NSLOOKUP HOST BRCTL NTPD \
         IP IPADDR IPLINK IPROUTE IPRULE IPTUNNEL IPNEIGH IPNETNS \
         TC FEATURE_IPV6 FEATURE_UNIX_LOCAL FEATURE_PREFER_APPLETS \
         SLATTACH ZCIP RFKILL NAMEIF TUNCTL VCONFIG \
         ETHER_WAKE IPCALC INETD WHOIS SENDMAIL; do
    dis "$o"
done

echo "[*] Disabling heavy / NOMMU-unsafe / unnecessary applets..."
for o in VI FEATURE_VI_REGEX AWK \
         KLOGD SYSLOGD LOGGER \
         INSMOD RMMOD LSMOD MODPROBE MODPROBE_SMALL DEPMOD \
         FDISK MKFS_EXT2 MKFS_MINIX FSCK FSCK_EXT2 E2FSCK \
         TUNE2FS RESIZE2FS BLKID FINDFS \
         TAR CPIO AR BUNZIP2 BZIP2 GUNZIP GZIP LZOP UNLZMA LZMA \
         UNXZ XZ ZCAT BZCAT LZCAT UNZIP \
         WGET CURL \
         CRONTAB CROND AT ATD \
         SENDMAIL MAKEMIME \
         SVLOGD RUNSV RUNSVDIR RUNIT CHPST SV SVOK \
         SULOGIN SU LOGIN PASSWD GETTY VLOCK ADDUSER ADDGROUP \
         DELUSER DELGROUP \
         CHPASSWD CHSH CRYPTPW MKPASSWD \
         SETUIDGID SOFTLIMIT \
         CHCON GETENFORCE SETENFORCE RUNCON \
         NANDWRITE NANDDUMP MTD_DEBUG \
         FLASHCP \
         SEEDRNG \
         UNSHARE NSENTER SETARCH \
         CTTYHACK \
         SETSID TASKSET \
         STRACE \
         LINUX32 LINUX64 \
         LSPCI SETPCI \
         I2CGET I2CSET I2CDUMP I2CDETECT \
         HDPARM \
         READPROFILE \
         IONICE \
         CHRT \
         FALLOCATE \
         IOLIMIT \
         SCRIPTREPLAY SCRIPT \
         FACTOR PRINTF \
         FTP FTPD \
         TELNETD \
         TFTPD TFTP \
         POPMAILDIR REFORMIME MAKEMIME; do
    dis "$o"
done

echo "[*] Keeping only essential applets..."
# These are explicitly kept (ensure they're enabled)
for o in INIT FEATURE_USE_INITTAB FEATURE_INIT_QUIET \
         HALT POWEROFF REBOOT \
         HUSH \
         CAT ECHO LS MKDIR RM RMDIR CP MV LN TOUCH PWD \
         BASENAME DIRNAME READLINK STAT \
         GREP EGREP FGREP SED CUT HEAD TAIL WC \
         DMESG ENV SLEEP USLEEP UNAME HOSTNAME DATE \
         FREE DF PS KILL KILLALL \
         MOUNT UMOUNT DD SYNC CLEAR FIND XARGS \
         TRUE FALSE TEST TEST1 EXPR; do
    en "$o"
done

echo "[*] Building (target: busybox_unstripped — cross-strip doesn't handle bFLT)..."
make ARCH=m68k CROSS_COMPILE="$CROSS" -j"$(nproc)" busybox_unstripped

echo "[*] Verifying bFLT format..."
file busybox_unstripped | grep -q "BFLT" || {
    echo "ERROR: busybox_unstripped is not bFLT!"
    file busybox_unstripped
    exit 1
}

echo "[*] Copying output..."
cp busybox_unstripped "$SCRIPT_DIR/busybox_nommu"
# Keep the ELF-with-symbols (elf2flt emits <name>.gdb) for disassembly/debugging.
[ -f busybox_unstripped.gdb ] && cp busybox_unstripped.gdb "$SCRIPT_DIR/busybox_nommu.gdb"

echo "[*] Cleaning build directory..."
cd "$SCRIPT_DIR"
rm -rf "$BUILD_DIR"

echo
echo "[+] Done: $SCRIPT_DIR/busybox_nommu"
file "$SCRIPT_DIR/busybox_nommu"
ls -lh "$SCRIPT_DIR/busybox_nommu"
