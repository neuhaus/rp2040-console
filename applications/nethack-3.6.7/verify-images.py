import struct, subprocess, argparse
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('--build-dir', required=True)
parser.add_argument('--soft-reboot', action='store_true')
args = parser.parse_args()
b = Path(args.build_dir)

file_symbols = subprocess.check_output(
    ['arm-none-eabi-gcc-nm', '-u',
     str(b / 'CMakeFiles/nethack.dir/src/files.c.o')], text=True)
assert file_symbols.strip(), 'Empty lock-code symbol probe'
assert not any(line.split()[-1] == 'link'
               for line in file_symbols.splitlines() if line.split()), \
    'NetHack lock code must use NO_FILE_LINKS on FAT'
print('PASS: NetHack file locks use exclusive file creation, not unsupported hard links')

assert (b / 'nethack.bin').stat().st_size <= 0x200000, 'Firmware exceeds 2 MB flash'

ranges = []
for name in ['nethack.uf2', 'nethack-volume.uf2']:
    data = (b / name).read_bytes()
    assert len(data) % 512 == 0
    blocks = []
    for i in range(0, len(data), 512):
        magic0, magic1, flags, addr, size, index, count, family = \
            struct.unpack_from('<8I', data, i)
        assert (magic0, magic1) == (0x0a324655, 0x9e5d5157)
        assert struct.unpack_from('<I', data, i + 508)[0] == 0x0ab16f30
        assert flags & 0x2000 and family == 0xe48bff56 and size == 256
        assert index == i // 512 and count == len(data) // 512
        blocks.append((addr, addr + size))
    ranges.append((min(x[0] for x in blocks), max(x[1] for x in blocks)))
# The volume sits at the manifest's FS_OFFSET and runs to the top of flash;
# take the boundary from the volume image itself so it cannot drift from the
# manifest.
fs_xip = ranges[1][0]
assert ranges[1][1] == 0x10200000, 'Volume must end at the top of 2 MB flash'
assert 0x10000000 + (b / 'nethack.bin').stat().st_size <= fs_xip, \
    'Firmware overlaps data volume'
assert 0x10000000 <= ranges[0][0] < ranges[0][1] <= fs_xip

symbols = {}
for line in subprocess.check_output(
        ['arm-none-eabi-nm', '-n', str(b / 'nethack.elf')], text=True).splitlines():
    parts = line.split()
    if len(parts) == 3:
        symbols[parts[2]] = int(parts[0], 16)
assert 0x10000000 <= symbols['main'] < fs_xip, 'main does not execute from flash'
assert 0x10000000 <= symbols['rp2040_console_game_main'] < fs_xip
assert symbols['__HeapLimit'] == 0x2003d000 \
    and symbols['__bss_end__'] < symbols['__HeapLimit'] < symbols['__StackBottom']
print('PASS: XIP entry points, 2 MB flash bounds, UF2 families/blocks, nonoverlapping images, heap/stack bounds')
print('Firmware bytes:', (b / 'nethack.bin').stat().st_size,
      'Static RAM:', symbols['__bss_end__'] - 0x20000000,
      'Heap budget:', symbols['__HeapLimit'] - symbols['__bss_end__'])

if args.soft_reboot:
    data_start = symbols['__rp2040_program_data_start']
    data_end = symbols['__rp2040_program_data_end']
    bss_start = symbols['__rp2040_program_bss_start']
    bss_end = symbols['__rp2040_program_bss_end']
    load = symbols['__rp2040_program_data_load']
    assert 0x20000000 <= data_start < data_end <= bss_start < bss_end <= symbols['__bss_end__']
    assert 0x10000000 <= load < load + data_end - data_start <= symbols['__flash_binary_end']
    assert 'rp2040_soft_run' in symbols and '__wrap__malloc_r' in symbols
    print('PASS: soft reboot program globals isolated from platform; initial data image in XIP; session allocator linked')
