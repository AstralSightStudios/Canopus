/* Compact ET_REL layout for the exact-target supervisor.
 *
 * Merge function/data sections so ELF section-table overhead stays below the
 * stock modlib 64 KiB file-size limit. Constructors and destructors remain distinct.
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
