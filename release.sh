#!/usr/bin/env bash
#
# Builds a bootable Linux image for each of the four boards
#
# Artifacts:
#   mackerel-08-kernel-*.bin
#   mackerel-08-rom-*.bin
#   mackerel-08-sd-*.img.gz
#   mackerel-10-kernel-*.bin
#   mackerel-10-cf-*.img.gz
#   mackerel-30-kernel-*.bin
#   mackerel-30-cf-*.img.gz
#   mackerel-f-kernel-*.bin
#   mackerel-f-sd-*.img.gz

set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
FW="$REPO/../mackerel-68k/firmware" # assumes the base mackerel-68k repo is in the same folder as this mackerel-linux repo

export PATH="$PATH:$HOME/x-tools/m68k-mackerel-elf/bin"   # bare-metal (mack08 bootloader)

# Version from the kernel, dated today; output always goes to dist/.
VERSION="$(make -C "$REPO" -s kernelversion 2>/dev/null)"
[ -z "$VERSION" ] && { echo "ERROR: could not read kernel version"; exit 1; }
STAMP="v$VERSION-$(date +%Y-%m-%d)"
STAGE="$REPO/dist/mackerel-linux-$STAMP"

# Releases must build from a committed kernel tree. With CONFIG_LOCALVERSION_AUTO=y the
# banner carries the HEAD commit (-g<hash>) plus a -dirty flag whenever any TRACKED file
# is modified (scripts/setlocalversion, -uno -- untracked files like this script are fine).
# Fail fast so no image ships as -dirty; the -g<hash> then pins the exact release commit.
DIRTY="$(git -C "$REPO" status -uno --porcelain 2>/dev/null)"
if [ -n "$DIRTY" ]; then
    echo "ERROR: kernel tree has uncommitted tracked changes; a release would boot -dirty." >&2
    echo "       Commit or stash these, then re-run:" >&2
    git -C "$REPO" status -uno --short >&2
    exit 1
fi

STAGED=0
FAILED=0
have() { echo "  [ok]   $1"; STAGED=$((STAGED + 1)); }
fail() { echo "  [FAIL] $*" >&2; FAILED=$((FAILED + 1)); }

# Copy a built artifact into staging (normalizing mode), or record a failure.
stage() {
    local src="$1" name="$2"
    if [ -f "$src" ] && install -m 0644 "$src" "$STAGE/$name"; then have "$name"; else fail "$name -- could not stage from $src"; fi
}

# busybox -> rootfs -> kernel for a board; leaves image.bin in the repo root.
#   $1 board id (08/10/30/f)
build_linux() {
    local board="$1" log="/tmp/rel_linux_$1.log"
    echo "  building mackerel-$board (busybox + rootfs + kernel)..."
    if ( cd "$REPO" && bash build_busybox.sh "$board" \
                    && bash build_rootfs.sh  "$board" \
                    && bash build_kernel.sh  "$board" ) >"$log" 2>&1; then
        return 0
    fi
    fail "mackerel-$board linux -- build failed (see $log)"
    return 1
}

# build_rootfs.sh 08 assembles rom08.bin from the sibling mack08 bootloader; build it first.
build_mack08_bootloader() {
    echo "  building mack08 bootloader (from mackerel-68k repo)..."
    if ( make -C "$FW" clean && make -C "$FW" BOARD=mack08 bootloader.bin ) >/tmp/rel_linux_08_bl.log 2>&1; then
        return 0
    fi
    fail "mackerel-08 rom -- mack08 bootloader build failed (see /tmp/rel_linux_08_bl.log)"
    return 1
}

# Full dd-able disk image: MBR + FAT16 boot (IMAGE.BIN), optionally + an ext4 root.
# Assembled rootless (mtools + mke2fs + sfdisk + dd). 1 MiB align, 16 MiB FAT16 boot.
#   $1 output artifact name
#   $2 ext4 root: a rootfs dir to populate it, "empty" for a blank ext4, or "none" (FAT only)
build_disk_image() {
    local outname="$1" root_spec="$2" img="$REPO/image.bin"
    local log="/tmp/rel_img_$(echo "$outname" | tr -c 'A-Za-z0-9' _).log"
    if ! command -v mcopy >/dev/null; then
        fail "$outname -- mtools (mcopy) not found on PATH"; return
    fi
    [ -f "$img" ] || { fail "$outname -- image.bin missing"; return; }
    case "$root_spec" in
        none|empty) ;;
        *) [ -d "$root_spec" ] || { fail "$outname -- rootfs $root_spec missing"; return; } ;;
    esac

    local boot_start=2048 boot_sect=32768            # 1 MiB align; 16 MiB FAT16 boot
    local root_start=$((boot_start + boot_sect)) root_mib=0
    case "$root_spec" in
        none)  ;;                                     # FAT only, no ext partition
        empty) root_mib=64 ;;                         # blank ext4 placeholder
        *)     local used; used=$(du -sm "$root_spec" | cut -f1)   # 2x content, floor 100 MiB
               root_mib=$(( used * 2 > 100 ? used * 2 : 100 )) ;;
    esac
    local root_sect=$((root_mib * 2048)) total_sect=$((root_start + root_mib * 2048))

    local work; work="$(mktemp -d /tmp/rel_img.XXXXXX)"
    local boot="$work/boot.img" root="$work/root.img" disk="$work/disk.img"
    local desc="16 MiB FAT16 boot"; [ "$root_mib" -gt 0 ] && desc="$desc + ${root_mib} MiB ext4 root"

    echo "  building $outname ($desc)..."
    {
        mkfs.fat -F 16 -n MACKBOOT -h "$boot_start" -C "$boot" $((boot_sect / 2)) &&
        mcopy -i "$boot" "$img" ::/IMAGE.BIN &&
        truncate -s $((total_sect * 512)) "$disk" &&
        if [ "$root_mib" -eq 0 ]; then
            printf 'label: dos\nstart=%d, size=%d, type=0e, bootable\n' \
                "$boot_start" "$boot_sect" | sfdisk -q "$disk" &&
            dd if="$boot" of="$disk" bs=512 seek="$boot_start" conv=notrunc status=none
        else
            case "$root_spec" in
                empty) mke2fs -q -F -t ext4 -L mackerel "$root" ${root_mib}M ;;
                *)     fakeroot sh -c "chown -R 0:0 '$root_spec' && mke2fs -q -F -t ext4 -L mackerel -d '$root_spec' '$root' ${root_mib}M" ;;
            esac &&
            printf 'label: dos\nstart=%d, size=%d, type=0e, bootable\nstart=%d, size=%d, type=83\n' \
                "$boot_start" "$boot_sect" "$root_start" "$root_sect" | sfdisk -q "$disk" &&
            dd if="$boot" of="$disk" bs=512 seek="$boot_start" conv=notrunc status=none &&
            dd if="$root" of="$disk" bs=512 seek="$root_start" conv=notrunc status=none
        fi &&
        gzip -f "$disk"
    } >"$log" 2>&1
    if [ -f "$disk.gz" ]; then stage "$disk.gz" "$outname"; else fail "$outname -- image assembly failed (see $log)"; fi
    rm -rf "$work"
}

# --- Build ------------------------------------------------------------------
echo "==> Mackerel-68k Linux release $STAMP -> $STAGE"
rm -rf "$STAGE"; mkdir -p "$STAGE"

echo "-- Mackerel-08 --"
if build_mack08_bootloader && build_linux 08; then
    stage "$REPO/image.bin" "mackerel-08-kernel-$STAMP.bin"
    stage "$REPO/rom08.bin" "mackerel-08-rom-$STAMP.bin"
    build_disk_image "mackerel-08-sd-$STAMP.img.gz" none    # no ext (mack08 Linux has none)
fi

echo "-- Mackerel-10 --"
if build_linux 10; then
    stage "$REPO/image.bin" "mackerel-10-kernel-$STAMP.bin"
    build_disk_image "mackerel-10-cf-$STAMP.img.gz" empty
fi

echo "-- Mackerel-30 --"
if build_linux 30; then
    stage "$REPO/image.bin" "mackerel-30-kernel-$STAMP.bin"
    build_disk_image "mackerel-30-cf-$STAMP.img.gz" "$REPO/rootfs_mackerel30"
fi

echo "-- Mackerel-F --"
if build_linux f; then
    stage "$REPO/image.bin" "mackerel-f-kernel-$STAMP.bin"
    build_disk_image "mackerel-f-sd-$STAMP.img.gz" empty
fi

if [ "$FAILED" -gt 0 ]; then
    echo
    echo "==================================================================="
    echo "  RELEASE ABORTED: $FAILED subtask(s) did not build cleanly."
    echo "  No release produced ($STAGED built artifact(s) discarded)."
    echo "  See the /tmp/rel_linux_*.log (and /tmp/rel_img_*.log) files."
    echo "==================================================================="
    rm -rf "$STAGE"
    exit 1
fi

# --- README + checksums (only reached on a fully clean build) ---------------
cat >"$STAGE/README.md" <<'EOF'
# Mackerel 68k Linux release (@STAMP@)

Prebuilt, bootable Linux images (mainline Linux @VERSION@) for the four
[Mackerel 68k](https://github.com/crmaykish/mackerel-68k) boards. The bootloader's
`boot` command loads `IMAGE.BIN` from the FAT partition of an SD/CF card into RAM and
runs it. (Mackerel-F can also `netboot` it over Ethernet.)

| Board | Kernel image | Root filesystem | Card image |
|-------|--------------|-----------------|------------|
| Mackerel-08 | `mackerel-08-kernel-@STAMP@.bin` | in the boot ROM (flash it, below) | `mackerel-08-sd-@STAMP@.img.gz` |
| Mackerel-10 | `mackerel-10-kernel-@STAMP@.bin` | XIP ROMfs (baked in) | `mackerel-10-cf-@STAMP@.img.gz` |
| Mackerel-30 | `mackerel-30-kernel-@STAMP@.bin` | on the CF card | `mackerel-30-cf-@STAMP@.img.gz` |
| Mackerel-F  | `mackerel-f-kernel-@STAMP@.bin`  | XIP ROMfs (baked in) | `mackerel-f-sd-@STAMP@.img.gz` |

## Fresh install — write the card image

The `*.img.gz` files are complete disk images (MBR + a FAT16 boot partition holding
`IMAGE.BIN`; -10/-30/-F also carry an ext4 partition). Decompress and `dd` one to a card,
then boot:

```sh
gunzip -c mackerel-30-cf-@STAMP@.img.gz | sudo dd of=/dev/sdX bs=4M conv=fsync
```

- **Mackerel-30** — the ext4 partition is the real root (`/`).
- **Mackerel-10 / -F** — the root is the baked-in ROMfs; the ext4 partition is **empty**
  (Mackerel-F mounts it at `/root`; grow it with `resize2fs` to fill the card).
- **Mackerel-08** — FAT only (no ext); the root filesystem lives in the boot ROM (below).

## Update the kernel only

On an already-written card, just replace the kernel — copy the board's
`*-kernel-@STAMP@.bin` onto the card's FAT (boot) partition, renamed `IMAGE.BIN`:

```sh
sudo mount /dev/sdX1 /mnt
sudo cp mackerel-30-kernel-@STAMP@.bin /mnt/IMAGE.BIN
sudo umount /mnt
```

`boot` picks it up on the next reset.

## Mackerel-08 — boot ROM

The `-08` root filesystem is in the boot ROM. Flash `mackerel-08-rom-@STAMP@.bin`
(bootloader + read-only ROMfs, 512 KB) once with `minipro` (`SST39SF040`):

```sh
minipro -p SST39SF040 -w mackerel-08-rom-@STAMP@.bin
```

## Verify

```sh
sha256sum -c SHA256SUMS
```
EOF
sed -i "s/@STAMP@/$STAMP/g; s/@VERSION@/$VERSION/g" "$STAGE/README.md"

( cd "$STAGE" && sha256sum *.bin *.img.gz > SHA256SUMS )

echo
echo "==================================================================="
echo "  Mackerel-68k Linux release $STAMP"
echo "  staging: $STAGE"
echo "  artifacts: $STAGED staged (all built cleanly)"
echo "==================================================================="
