/* Keeps debug info and embedded LLVM bitcode out of a Rust module: they are not
 * loaded, and their relocations make the Canopus ELF loader reject the image. */
SECTIONS { /DISCARD/ : { *(.debug_*) *(.llvmbc) *(.llvmcmd) } }
