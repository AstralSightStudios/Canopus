ENTRY(canopus_band11_stage1_entry)
SECTIONS {
    . = 0;
    .text : { *(.text.entry) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.* .got .got.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx* .comment .note* .ARM.attributes) }
}
