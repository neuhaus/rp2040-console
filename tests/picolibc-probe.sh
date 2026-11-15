#!/bin/sh
# Probe how far a picolibc port is from building.
#
# picolibc is the arm-none-eabi C library in current distributions (Ubuntu
# 26.10, Debian sid) and dropped newlib's reentrant API that the soft-reboot
# session core is written against. This is a tripwire, not a build: while the
# port is unstarted the compile fails, and a successful compile is the
# milestone signal that the port may be feasible. See PICOLIBC.md.
set -eu
cd "$(dirname "$0")/.."

# Locate the distribution picolibc; PICOLIBC_ROOT overrides it for a local
# run against an unpacked package.
root=${PICOLIBC_ROOT:-}
if [ -z "$root" ]; then
    libc=$(find /usr/lib/picolibc -name libc.a 2>/dev/null | head -1)
    if [ -z "$libc" ]; then
        echo "error: picolibc not found; install picolibc-arm-none-eabi" >&2
        exit 2
    fi
    root=${libc%%/lib/*}
fi
libc=$(find "$root/lib" -name libc.a 2>/dev/null | head -1)
inc=$root/include
nm=${NM:-arm-none-eabi-nm}
echo "picolibc: $root"

echo "required newlib API, and what picolibc provides:"
for h in reent.h sys/reent.h stdio_ext.h; do
    if [ -e "$inc/$h" ]; then s=present; else s=MISSING; fi
    printf '  header %-14s %s\n' "$h" "$s"
done
for sym in _impure_ptr __getreent _malloc_r _free_r _calloc_r _realloc_r \
           _open_r _read_r _write_r _lseek_r _setlocale_r _tzset_r _sbrk; do
    if "$nm" --defined-only "$libc" 2>/dev/null | grep -qE " [TDBRWV] $sym\$"; then
        s=present
    else
        s=MISSING
    fi
    printf '  symbol %-14s %s\n' "$sym" "$s"
done

# Compile against picolibc alone. -nostdinc keeps the host's newlib headers
# out of the search path, so this measures the real header and API gap rather
# than the host toolchain's defaults.
builtin=$(arm-none-eabi-gcc -print-file-name=include)
flags="-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -std=gnu11 -nostdinc
-isystem $builtin -isystem $inc -Iinclude -Isrc -Itests/support"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM

echo "compiling the session core against picolibc:"
status=0
for f in src/soft_reboot.c src/soft_reboot_libc.c src/nethack_pool.c; do
    if arm-none-eabi-gcc $flags -c "$f" -o "$work/$(basename "$f" .c).o" 2>"$work/err"; then
        printf '  %-26s ok\n' "$f"
    else
        printf '  %-26s failed\n' "$f"
        sed -n '1,2p' "$work/err" | sed 's/^/      /'
        status=1
    fi
done

if [ "$status" -eq 0 ]; then
    echo "RESULT: the session core compiles against picolibc"
else
    echo "RESULT: the session core does not compile against picolibc (expected)"
fi
exit "$status"
