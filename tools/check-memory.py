#!/usr/bin/env python3
"""Validate the linked image against the manifest's flash and SRAM budgets."""
import argparse
import subprocess


def number(value):
    return int(value, 0)


def validate(symbols, flash_size, fs_offset, fs_size, stack_reserve=0, copy_to_ram=False):
    required = ("__flash_binary_end", "__bss_end__", "__HeapLimit", "__StackBottom", "__StackTop", "main")
    missing = set(required) - symbols.keys()
    if missing:
        raise ValueError("missing linker symbols: " + ", ".join(sorted(missing)))
    if fs_offset <= 0 or fs_size <= 0 or fs_offset % 4096 or fs_size % 4096:
        raise ValueError("FAT offset/size must be positive multiples of 4096")
    if fs_offset + fs_size > flash_size:
        raise ValueError("FAT volume exceeds flash capacity")
    firmware_size = symbols["__flash_binary_end"] - 0x10000000
    if not 0 < firmware_size <= fs_offset:
        raise ValueError("firmware overlaps FAT volume or has invalid flash bounds")
    if not copy_to_ram and not 0x10000000 <= symbols["main"] < 0x10000000 + fs_offset:
        raise ValueError("main does not execute from XIP flash")
    static_end, heap_end = symbols["__bss_end__"], symbols["__HeapLimit"]
    if not 0x20000000 < static_end < heap_end <= symbols["__StackBottom"] < symbols["__StackTop"] <= 0x20042000:
        raise ValueError("static data, heap and stack do not fit RP2040 SRAM")
    reserve = symbols["__StackTop"] - heap_end
    if stack_reserve < 0 or reserve < stack_reserve:
        raise ValueError("space above heap is smaller than STACK_RESERVE")
    return (firmware_size, static_end - 0x20000000, heap_end - static_end, reserve)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True)
    parser.add_argument("--nm", required=True)
    for name in ("flash-size", "fs-offset", "fs-size"):
        parser.add_argument("--" + name, type=number, required=True)
    parser.add_argument("--stack-reserve", type=number, default=0)
    parser.add_argument("--copy-to-ram", type=number, default=0)
    args = parser.parse_args()
    symbols = {}
    for line in subprocess.check_output([args.nm, "-n", args.elf], text=True).splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    try:
        flash, static, heap, reserve = validate(symbols, args.flash_size, args.fs_offset,
                                               args.fs_size, args.stack_reserve, bool(args.copy_to_ram))
    except ValueError as error:
        parser.exit(1, f"memory check failed: {error}\n")
    print(f"Memory: firmware {flash} B / {args.fs_offset} B; static SRAM {static} B; "
          f"heap {heap} B; space above heap {reserve} B (stack/scratch, peak unmeasured)")


if __name__ == "__main__":
    main()
