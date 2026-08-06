/* Compact ET_REL layout for the exact-target supervisor.
 *
 * Monocypher is compiled with function sections for garbage collection. Merge
 * retained sections again so ELF section-table overhead stays below the stock
 * modlib 64 KiB file-size limit. Constructors and destructors remain distinct.
 */
SECTIONS
{
    .text : { *(.text .text.*) }
    .rodata : { *(.rodata .rodata.*) }
    .data : { *(.data .data.*) }
    .bss : { *(.bss .bss.* COMMON) }
    .init_array : { *(.init_array .init_array.*) }
    .fini_array : { *(.fini_array .fini_array.*) }
}
