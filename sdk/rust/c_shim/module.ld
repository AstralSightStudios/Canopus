/* Merges per-function and per-datum sections into canonical ELF regions so
 * the section count stays well within the freestanding loader's limit
 * (CANOPUS_ELF32_MAX_SECTIONS = 256). Discards debug info, unwinding tables
 * (panic = "abort") and embedded LLVM bitcode. */
SECTIONS {
    .text : {
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss : {
        *(.bss .bss.*)
    }
    /DISCARD/ : {
        *(.debug_*)
        *(.llvmbc)
        *(.llvmcmd)
        *(.comment)
        *(.ARM.attributes)
        *(.ARM.exidx*)
        *(.rel.ARM.exidx*)
    }
}
