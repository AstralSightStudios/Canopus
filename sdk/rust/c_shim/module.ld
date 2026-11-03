/* Merges per-function and per-datum sections into canonical ELF regions so
 * the image keeps a handful of sections, which keeps the module file
 * smaller and the image cheaper to load. Discards debug info, unwinding tables
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
