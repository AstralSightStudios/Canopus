ENTRY(canopus_band9_stage2_entry)
SECTIONS
{
    . = 0;
    .text : {
        *(.text.canopus_band9_stage2_entry)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    /DISCARD/ : { *(.ARM.exidx* .comment .note* .ARM.attributes) }
}
