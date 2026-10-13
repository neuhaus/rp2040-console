#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
root=$PWD
flags=(-mcpu=cortex-m0plus -mthumb -mfloat-abi=soft -fomit-frame-pointer -std=gnu11 -O2 -g -ffunction-sections -fdata-sections -fno-common -I"$root/include" -I"$root/src")
printf '%s\n' "$root/tests/soft/program.c" > "$work/sources.txt"
python3 tools/tag-program-state.py --sources "$work/sources.txt" --objcopy arm-none-eabi-objcopy --objdump arm-none-eabi-objdump -- arm-none-eabi-gcc "${flags[@]}" -c "$root/tests/soft/program.c" -o "$work/program.o"
for source in src/soft_reboot.c src/soft_reboot_libc.c src/heap.c tests/soft/{control,linux}.c; do
    arm-none-eabi-gcc "${flags[@]}" -c "$source" -o "$work/$(basename "$source").o"
done
arm-none-eabi-gcc "${flags[@]}" -c tests/soft/start.S -o "$work/start.o"
arm-none-eabi-gcc "${flags[@]}" -nostartfiles -Wl,-T,tests/soft/memory.ld -Wl,--gc-sections \
    -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r -Wl,--wrap=_free_r \
    -Wl,--wrap=_tzset_r -Wl,--wrap=_tzset_unlocked_r -Wl,--wrap=_setlocale_r \
    "$work"/*.o -o "$work/test.elf"
cd "$work"
qemu-arm -cpu cortex-a15 ./test.elf
