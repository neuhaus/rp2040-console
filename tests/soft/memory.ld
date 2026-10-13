ENTRY(_start)
SECTIONS {
    . = 0x10000;
    .text : { *(.text*) }
    .rodata : { *(.rodata*) }
    . = 0x20000000;
    .data : {
        *(.data*) *(.sdata*)
        . = ALIGN(8);
        test_program_data_start = .;
        *(.rp2040_program_data*)
        . = ALIGN(8);
        test_program_data_end = .;
    }
    .bss (NOLOAD) : {
        *(.bss*) *(COMMON)
        . = ALIGN(8);
        test_program_bss_start = .;
        *(.rp2040_program_bss*)
        . = ALIGN(8);
        test_program_bss_end = .;
    }
    .heap (NOLOAD) : { end = .; . += 2 * 1024 * 1024; __HeapLimit = .; }
}
