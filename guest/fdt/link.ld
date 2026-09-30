/* SPDX-License-Identifier: MIT */
/* Copyright (c) 2026 John Knipper */
ENTRY(_start)
SECTIONS {
    . = DEFINED(IMAGE_BASE) ? IMAGE_BASE : 0x40080000;
    .text : { KEEP(*(.text.entry)) *(.text*) }
    .rodata : { *(.rodata*) }
    .data : { *(.data*) }
    . = ALIGN(16);
    bss_start = .;
    .bss (NOLOAD) : { *(.bss*) *(COMMON) }
    . = ALIGN(16);
    bss_end = .;
    /DISCARD/ : { *(.comment) *(.eh_frame*) *(.note*) }
}
