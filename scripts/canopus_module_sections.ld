/* Stock modlib ET_REL layout. module_initialize targets must keep their
 * Thumb entry shim at the start of the merged executable section so e_entry
 * is relative to the text base used by the loader. */
SECTIONS
{
    .text : { KEEP(*(.text.canopus_module_entry)) *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    .init_array : { *(.init_array .init_array.*) }
    .fini_array : { *(.fini_array .fini_array.*) }
}
