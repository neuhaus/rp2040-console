#!/usr/bin/env python3
"""Compiler launcher: isolate mutable sections of declared program objects."""
import argparse
import pathlib
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sources', required=True)
    parser.add_argument('--objcopy', required=True)
    parser.add_argument('--objdump', required=True)
    args, command = parser.parse_known_args()
    if command and command[0] == '--': command.pop(0)
    if not command: parser.error('missing compiler command')
    result = subprocess.run(command)
    if result.returncode: return result.returncode if result.returncode > 0 else 128 - result.returncode
    if '-c' not in command or '-o' not in command: return 0
    source = pathlib.Path(command[command.index('-c') + 1]).resolve()
    sources = {pathlib.Path(line).resolve() for line in pathlib.Path(args.sources).read_text().splitlines()}
    if source not in sources: return 0  # SDK, FatFs and platform remain intact.
    output = command[command.index('-o') + 1]
    table = subprocess.check_output([args.objdump, '-t', output], text=True)
    if '*COM*' in table:
        parser.exit(1, f'{source}: SOFT_REBOOT requires -fno-common\n')
    rows = subprocess.check_output([args.objdump, '-h', output], text=True).splitlines()
    renames = []
    for index, line in enumerate(rows):
        fields = line.split()
        if len(fields) < 3 or not fields[0].isdigit(): continue
        name, size = fields[1], int(fields[2], 16)
        flags = rows[index + 1] if index + 1 < len(rows) else ''
        if not size: continue
        if name in ('.data', '.sdata') or name.startswith(('.data.', '.sdata.')):
            renames += ['--rename-section', f'{name}=.rp2040_program_data{name}']
        elif name in ('.bss', '.sbss') or name.startswith(('.bss.', '.sbss.')):
            renames += ['--rename-section', f'{name}=.rp2040_program_bss{name}']
        elif 'ALLOC' in flags and 'READONLY' not in flags and 'CODE' not in flags:
            parser.exit(1, f'{source}: unsupported mutable section {name} in SOFT_REBOOT mode\n')
    if renames: subprocess.run([args.objcopy, *renames, output], check=True)
    return 0


if __name__ == '__main__': sys.exit(main())
