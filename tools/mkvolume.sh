#!/bin/sh
#
# Build the FAT volume a game on RP2040 boots with.
#
# The filesystem is made here on the host and flashed as a UF2 of its own,
# which is worth the extra step for three reasons:
#
#   - the device never runs f_mkfs, so there is no formatting code in the
#     image and no format-on-first-boot delay or failure mode;
#   - the image can be checked with fsck.vfat before it ever reaches the
#     board, which cannot be done properly on the device;
#   - the game's data files go in where a real editor and a real diff exist.
#
# What goes in is not this script's business: it reads a manifest that the
# engine generates from the game's own declaration.
#
# Manifest format, one directive per line, '#' to end of line:
#
#   label <name>          volume label, 8 characters or fewer
#   offset <n>            flash offset the volume is mounted from
#   size <n>              volume size in bytes
#   flash_size <n>        flash capacity to check the above against
#   name <stem>           output is <stem>-volume.img/.uf2
#   flash <yes|no>        emit a UF2 for the board (default yes)
#   file <path> [name]    copy a host file in, under its own name by default
#   empty <name>          create an empty file on the volume
#   dir <name>            create a directory on the volume
#
# Requires dosfstools (mkfs.vfat, fsck.vfat), mtools (mcopy, mmd, mdir) and
# picotool, which the SDK builds next to the firmware.
#
# Usage: mkvolume.sh --manifest FILE --out DIR [--name STEM] [--picotool PATH]
#
set -e
set -f # Manifest names are literal, never shell glob patterns.

MANIFEST=""
OUTDIR=""
STEM=""
PICOTOOL=${PICOTOOL:-}

while [ $# -gt 0 ]; do
    case "$1" in
        --manifest) MANIFEST=$2; shift 2 ;;
        --out)      OUTDIR=$2;   shift 2 ;;
        --name)     STEM=$2;     shift 2 ;;
        --picotool) PICOTOOL=$2; shift 2 ;;
        *) echo "error: unknown argument $1" >&2; exit 2 ;;
    esac
done

if [ -z "$MANIFEST" ] || [ ! -f "$MANIFEST" ]; then
    echo "error: pass --manifest with a file describing the volume" >&2
    echo "       (the engine writes one to <builddir>/<game>-volume.txt)" >&2
    exit 2
fi

[ -n "$OUTDIR" ] || OUTDIR=$(dirname "$MANIFEST")

LABEL=GAMEFLASH
OFFSET=0x180000
SIZE=524288
FLASH_BYTES=$((2 * 1024 * 1024))
TO_FLASH=yes
FILES=""
EMPTIES=""
DIRS=""

# Read the manifest. Trailing comments are stripped; everything else is
# passed through verbatim so paths keep their spaces.
TAB=$(printf '\t')
while IFS= read -r line; do
    # Generated manifests use tabs so filenames can contain spaces. Keep
    # accepting the original whitespace-separated manifest format.
    case "$line" in
        *"$TAB"*) IFS="$TAB" read -r kind a b rest <<FIELDS
$line
FIELDS
            ;;
        *) read -r kind a b rest <<FIELDS
$line
FIELDS
            ;;
    esac
    [ -n "$kind" ] || continue
    case "$kind" in
        label)      LABEL=$a ;;
        offset)     OFFSET=$a ;;
        size)       SIZE=$a ;;
        flash_size) FLASH_BYTES=$a ;;
        flash)      TO_FLASH=$a ;;
        file)
            [ -n "$a" ] || { echo "error: 'file' with no path" >&2; exit 1; }
            #
            # Existence is checked here, at the point the manifest is read,
            # rather than when the file is copied in. A volume that is quietly
            # missing its data files is the worst kind of build success: the
            # image builds, flashes, and the program stops at startup on a
            # missing file, with the cause back in a manifest path that nobody
            # looks at again. The usual reason is a manifest that meant a
            # different directory - a root that defaulted somewhere else.
            #
            [ -f "$a" ] || {
                echo "error: manifest file '$a' does not exist" >&2
                echo "       (from a '$kind' entry in $MANIFEST)" >&2
                exit 1
            }
            case "$a${b}" in
                *'|'*) echo "error: manifest paths may not contain '|'" >&2; exit 1 ;;
            esac
            FILES="$FILES
$a|${b:-$(basename "$a")}"
            ;;
        empty)
            [ -n "$a" ] && EMPTIES="$EMPTIES|$a"
            ;;
        dir)
            [ -n "$a" ] && DIRS="$DIRS|$a"
            ;;
        *) echo "error: unknown manifest directive '$kind'" >&2; exit 1 ;;
    esac
done <<MANIFEST_BODY
$(sed -e 's/#.*$//' "$MANIFEST")
MANIFEST_BODY

if [ -z "$STEM" ]; then
    STEM=$(basename "$MANIFEST" | sed -e 's/-volume\.txt$//' -e 's/\.txt$//')
fi

# --- sanity before writing anything ------------------------------------
#
# An offset and size that run past the end of the chip produce a volume that
# half-mounts and then reads garbage, and nothing on the board can tell you
# so. Catch it here, where there is somebody to tell.
#
OFFSET_N=$((OFFSET))
SIZE_N=$((SIZE))
if [ "$OFFSET_N" -le 0 ] || [ "$SIZE_N" -le 0 ] ||
   [ $((OFFSET_N % 4096)) -ne 0 ] || [ $((SIZE_N % 4096)) -ne 0 ]; then
    echo "error: volume offset/size must be positive multiples of 4096" >&2
    exit 1
fi
if [ $((OFFSET_N + SIZE_N)) -gt "$FLASH_BYTES" ]; then
    overrun=$((OFFSET_N + SIZE_N - FLASH_BYTES))
    max=$((FLASH_BYTES - SIZE_N))
    echo "error: volume at $OFFSET + ${SIZE_N}B runs $overrun bytes past the end of $FLASH_BYTES bytes of flash" >&2
    echo "       the highest base for a ${SIZE_N} byte volume is ${max} (0x$(printf '%x' "$max"))" >&2
    exit 1
fi

# The RP2040 bootloader only accepts UF2 block addresses inside the XIP
# window, so the image has to be addressed at XIP_BASE + offset rather than
# at the bare offset. Not a detail: addressed at 0x180000 every block is
# ignored, the transfer never completes, and the board never reboots - so
# there is no failure message to wait for.
XIP_BASE=0x10000000
UF2_ADDR=$(printf "0x%x" $((XIP_BASE + OFFSET_N)))

# FAT12 for a small volume in flash: 512KB is below FAT16's minimum
# addressable size, and the type has to suit the size or mkfs emits
# something FatFs will not mount. Larger volumes, which means an SD card,
# get the type that fits them.
if   [ "$SIZE_N" -lt $((8 * 1024 * 1024)) ];  then FATTYPE=12
elif [ "$SIZE_N" -lt $((256 * 1024 * 1024)) ]; then FATTYPE=16
else                                            FATTYPE=32
fi

for tool in mkfs.vfat fsck.vfat mcopy mmd mdir; do
    if ! command -v "$tool" >/dev/null 2>&1 && [ ! -x "/usr/sbin/$tool" ]; then
        echo "error: $tool not found (Debian/Ubuntu: apt install dosfstools mtools)" >&2
        exit 1
    fi
done
MKFS=$(command -v mkfs.vfat || echo /usr/sbin/mkfs.vfat)
FSCK=$(command -v fsck.vfat || echo /usr/sbin/fsck.vfat)

if [ "$TO_FLASH" = yes ]; then
    if [ -z "$PICOTOOL" ]; then
        # The SDK builds picotool into the build tree it configures.
        for guess in "$OUTDIR/_deps/picotool/picotool" \
                     "$OUTDIR/../_deps/picotool/picotool"; do
            [ -x "$guess" ] && PICOTOOL=$guess && break
        done
    fi
    if [ ! -x "$PICOTOOL" ]; then
        : "${PICOTOOL:=picotool}"
        command -v picotool >/dev/null 2>&1 || {
            echo "error: picotool not found (set PICOTOOL=/path/to/picotool)" >&2
            echo "       the SDK builds it under <builddir>/_deps/picotool" >&2
            exit 1
        }
        PICOTOOL=$(command -v picotool)
    fi
fi

WORK=$OUTDIR/$STEM-volume-staging
rm -rf "$WORK"
mkdir -p "$WORK"
cd "$WORK"

IMG=$OUTDIR/$STEM-volume.img

echo "==> creating $IMG (${SIZE_N} bytes, FAT$FATTYPE)"
rm -f "$IMG"
truncate -s "$SIZE_N" "$IMG"
# 512-byte sectors: the flash driver reports that geometry and FatFs has to
# agree with it.  A small FAT12 volume in flash holds a game's own small
# state files (NetHack's level files are a few KB each), where 2 KB clusters
# leave up to 1.5 KB of slack per file; 512-byte clusters avoid that.  Larger
# volumes, which means an SD card, keep mkfs' size-appropriate default.
CLUSTER_OPT=
if [ "$FATTYPE" = 12 ]; then CLUSTER_OPT="-s 1"; fi
"$MKFS" -F "$FATTYPE" -n "$LABEL" -S 512 $CLUSTER_OPT "$IMG" >/dev/null

# --- contents ----------------------------------------------------------

ensure_dir() {
    case "$1" in
        ''|.) return ;;
        /*|..|../*|*/../*|*/..) echo "error: volume paths must be relative without '..'" >&2; exit 1 ;;
    esac
    if [ "$(dirname "$1")" != . ]; then
        ensure_dir "$(dirname "$1")"
    fi
    mmd -i "$IMG" "::$1" 2>/dev/null || mdir -i "$IMG" "::$1" >/dev/null
}

add_file() {
    src=$1
    name=$2

    if [ ! -f "$src" ]; then
        echo "error: required volume file disappeared: $src" >&2
        return 1
    fi

    ensure_dir "$(dirname "$name")"
    mcopy -i "$IMG" "$src" "::$name"

    # mcopy preserves nothing useful here and FAT names are 8.3 unless the
    # image is being read on a host; report what landed where.
    echo "    + $name  <- $src"
}

if [ -n "$FILES" ]; then
    echo "==> adding files"
    printf '%s\n' "$FILES" | while IFS='|' read -r src name; do
        [ -n "$src" ] || continue
        add_file "$src" "$name"
    done
fi

# Empty files, which a game expects an installation to have provided and
# never makes for itself. A score file is the usual case: the program opens
# it on exit, reports that it cannot, and never gets round to creating it -
# so on a fresh card the game is unplayable all the way to a clean exit.
#
# Empty is the correct content, not a placeholder. A score reader treats a
# short read as "no scores yet" and the first finished game writes itself
# in.
#
if [ -n "$EMPTIES" ]; then
    echo "==> creating empty files"
    oldifs=$IFS
    IFS='|'
    for name in $EMPTIES; do
        [ -n "$name" ] || continue
        ensure_dir "$(dirname "$name")"
        : > empty-file
        mcopy -i "$IMG" empty-file "::$name"
        rm -f empty-file
        echo "    + $name"
    done
    IFS=$oldifs
fi

# Directories, for games that write into a subdirectory. Without them a
# create returns FR_NO_PATH, which reads as "permission denied" to most
# programs and sends them looking for a permissions problem that is not
# there.
#
if [ -n "$DIRS" ]; then
    echo "==> creating directories"
    oldifs=$IFS
    IFS='|'
    for name in $DIRS; do
        [ -n "$name" ] || continue
        ensure_dir "$name"
        echo "    + $name/"
    done
    IFS=$oldifs
fi

echo "==> verifying with fsck"
if ! "$FSCK" -v "$IMG"; then
    echo "error: volume failed fsck - refusing to emit something unmountable" >&2
    exit 1
fi

echo "==> contents"
mdir -i "$IMG" ::

if [ "$TO_FLASH" != yes ]; then
    echo
    echo "built: $IMG  (not converted to UF2)"
    exit 0
fi

echo "==> converting to UF2 at flash offset $OFFSET (XIP $UF2_ADDR)"
cp "$IMG" volume.bin
"$PICOTOOL" uf2 convert volume.bin "$OUTDIR/$STEM-volume.uf2" \
    -o "$UF2_ADDR" --family rp2040 >/dev/null

# Verify the emitted UF2 rather than trusting it. Every failure mode at this
# step is silent: the bootloader accepts or ignores blocks without telling
# anyone, the board does not reboot, and there is nothing on the device to
# inspect afterwards because nothing landed.
python3 - "$OUTDIR/$STEM-volume.uf2" "$UF2_ADDR" "$XIP_BASE" "$FLASH_BYTES" <<'PYEOF'
import struct, sys

path = sys.argv[1]
want_addr = int(sys.argv[2], 0)
xip_base = int(sys.argv[3], 0)
flash_bytes = int(sys.argv[4], 0)

data = open(path, 'rb').read()
blocks = []
i = 0
while i + 512 <= len(data):
    m0, m1, _flags, tgt, size, blkno, nblk, fam = struct.unpack('<8I', data[i:i+32])
    if m0 == 0x0A324655 and m1 == 0x9E5D5157:
        blocks.append((tgt, size, blkno, nblk, fam))
    i += 512

if not blocks:
    print("error: no valid UF2 blocks found", file=sys.stderr)
    sys.exit(1)

fail = []

# 1. every target must be inside the XIP window and inside the chip
lo, hi = min(b[0] for b in blocks), max(b[0] for b in blocks) + 256
if lo < xip_base or hi > xip_base + flash_bytes:
    fail.append("targets 0x%08x..0x%08x fall outside the XIP window "
                "0x%08x..0x%08x" % (lo, hi, xip_base, xip_base + flash_bytes))

# 2. block numbers must be contiguous from 0: the bootloader writes them in
#    that order and stops at the first gap
nos = [b[2] for b in blocks]
if nos != list(range(len(nos))):
    fail.append("blockNo is not 0..N-1 in order")

# 3. numBlocks must match the count present, or the bootloader never sees
#    the transfer complete and never reboots
nb = {b[3] for b in blocks}
if nb != {len(blocks)}:
    fail.append("numBlocks field %s does not match the %d blocks present"
                % (sorted(nb), len(blocks)))

# 4. payload size and family must be uniform
if {b[1] for b in blocks} != {256}:
    fail.append("payload sizes are not all 256")
if len({b[4] for b in blocks}) != 1:
    fail.append("mixed familyID")

if fail:
    for f in fail:
        print("error: " + f, file=sys.stderr)
    sys.exit(1)

print("    verified %d blocks, 0x%08x..0x%08x, final blockNo %d == numBlocks-1"
      % (len(blocks), lo, hi, nos[-1]))
print("    the bootloader will reboot when this file finishes writing")
PYEOF

rm -rf "$WORK"

echo
echo "built: $OUTDIR/$STEM-volume.uf2  (image: $IMG)"
echo
echo "Flash BOTH files to the board:"
echo "  1. $STEM.uf2           (the program)"
echo "  2. $STEM-volume.uf2    (the FAT volume at $OFFSET)"
echo
echo "They do not overlap and the order does not matter, but the board should"
echo "reboot after the second one - if it does not, the copy did not land."
