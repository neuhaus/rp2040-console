#!/usr/bin/env python3
"""Insert program subranges into the SDK's normal startup data/BSS sections."""
import argparse
import pathlib


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--sdk', required=True)
parser.add_argument('--output', required=True)
args = parser.parse_args()
source = pathlib.Path(args.sdk) / 'src/rp2_common/pico_standard_link/script_include/section_default_data.incl'
script = source.read_text()
data_anchor = '        *(.sdata*)'
bss_anchor = '        *(.sbss*)'
if script.count(data_anchor) != 1 or script.count(bss_anchor) != 1:
    parser.exit(1, 'SOFT_REBOOT: unsupported SDK data linker layout; revalidate the section override\n')
script = script.replace(data_anchor, data_anchor + '''
        . = ALIGN(8);
        __rp2040_program_data_start = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rp2040_program_data*)))
        . = ALIGN(8);
        __rp2040_program_data_end = .;
        __rp2040_program_data_load = LOADADDR(.data) + (__rp2040_program_data_start - ADDR(.data));
''')
script = script.replace(bss_anchor, bss_anchor + '''
        . = ALIGN(8);
        __rp2040_program_bss_start = .;
        *(SORT_BY_ALIGNMENT(SORT_BY_NAME(.rp2040_program_bss*)))
        . = ALIGN(8);
        __rp2040_program_bss_end = .;
''')
pathlib.Path(args.output).write_text(script)
