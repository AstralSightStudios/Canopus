ENTRY(canopus_band9_stage1_entry)
SECTIONS
{
    . = 0;
    .text : {
        *(.text.canopus_band9_stage1_entry)
        *(.text .text.*)
    }
    .rodata : { *(.rodata .rodata.*) }
    /DISCARD/ : { *(.ARM.exidx* .comment .note* .ARM.attributes) }
}
