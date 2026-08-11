#include "canopus_elf32_loader.h"

#include "canopus_arm_reloc.h"
#include "canopus_memory.h"

#define EI_CLASS 4u
#define EI_DATA 5u
#define ELFCLASS32 1u
#define ELFDATA2LSB 1u
#define ET_REL 1u
#define EM_ARM 40u
#define EV_CURRENT 1u
#define ELF32_EHDR_SIZE 52u
#define ELF32_SHDR_SIZE 40u
#define ELF32_SYM_SIZE 16u
#define ELF32_REL_SIZE 8u
#define SHT_NULL 0u
#define SHT_PROGBITS 1u
#define SHT_SYMTAB 2u
#define SHT_STRTAB 3u
#define SHT_RELA 4u
#define SHT_NOBITS 8u
#define SHT_REL 9u
#define SHT_INIT_ARRAY 14u
#define SHT_FINI_ARRAY 15u
#define SHT_PREINIT_ARRAY 16u
#define SHT_ARM_EXIDX 0x70000001u
#define SHF_WRITE 1u
#define SHF_ALLOC 2u
#define SHF_EXECINSTR 4u
#define SHN_UNDEF 0u
#define SHN_ABS 0xfff1u
#define SHN_COMMON 0xfff2u

struct section_info {
    uint32_t memory_offset;
    uint32_t kind;
    uint8_t loaded;
};

static uint16_t u16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int range_ok(uint32_t offset, uint32_t length, uint32_t size)
{
    return offset <= size && length <= size - offset;
}

static int power_of_two(uint32_t value)
{
    return value != 0 && (value & (value - 1u)) == 0;
}

static int align_up(uint32_t value, uint32_t alignment, uint32_t *result)
{
    uint32_t mask;

    if (!power_of_two(alignment)) {
        return -1;
    }
    mask = alignment - 1u;
    if (value > UINT32_MAX - mask) {
        return -1;
    }
    *result = (value + mask) & ~mask;
    return 0;
}

static const uint8_t *shdr(const uint8_t *elf, uint32_t shoff,
                           uint32_t index)
{
    return elf + shoff + index * ELF32_SHDR_SIZE;
}

static uint32_t section_kind(uint32_t flags)
{
    if ((flags & SHF_EXECINSTR) != 0) {
        return CANOPUS_ELF_REGION_EXEC;
    }
    if ((flags & SHF_WRITE) != 0) {
        return CANOPUS_ELF_REGION_RW;
    }
    return CANOPUS_ELF_REGION_RO;
}

static int name_equals(const uint8_t *table, uint32_t table_size,
                       uint32_t offset, const char *expected)
{
    uint32_t i = 0;

    if (offset >= table_size) {
        return 0;
    }
    while (expected[i] != '\0') {
        if (offset + i >= table_size || table[offset + i] != (uint8_t)expected[i]) {
            return 0;
        }
        i++;
    }
    return offset + i < table_size && table[offset + i] == 0;
}

static int append_array(uint32_t *array, uint32_t *count,
                        const uint8_t *memory, uint32_t bytes,
                        uint32_t exec_start, uint32_t exec_end)
{
    uint32_t i;
    uint32_t value;

    if ((bytes & 3u) != 0 || bytes / 4u > CANOPUS_ELF32_MAX_ARRAY_ENTRIES - *count) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    for (i = 0; i < bytes; i += 4u) {
        value = u32(memory + i);
        if (value == 0 || value == UINT32_MAX) {
            continue;
        }
        if ((value & 1u) == 0 || (value & ~1u) < exec_start ||
            (value & ~1u) >= exec_end) {
            return CANOPUS_ELF_LOAD_CONSTRUCTORS;
        }
        array[(*count)++] = value;
    }
    return CANOPUS_ELF_LOAD_OK;
}

static void release_module(const struct canopus_elf_loader_ops *ops,
                           struct canopus_elf_module *module)
{
    if (module->allocation != 0 && ops->release != 0) {
        ops->release(ops->cookie, module->allocation, module->allocation_size);
    }
    canopus_memset(module, 0, sizeof(*module));
}

int canopus_elf32_load(const uint8_t *elf, uint32_t elf_size,
                       const struct canopus_elf_loader_ops *ops,
                       struct canopus_elf_module *module)
{
    struct section_info sections[CANOPUS_ELF32_MAX_SECTIONS];
    const uint8_t *section;
    const uint8_t *target;
    const uint8_t *symtab;
    const uint8_t *symbol_entry;
    const uint8_t *rel;
    const uint8_t *shstr;
    uint8_t *memory;
    uint32_t shoff;
    uint32_t shnum;
    uint32_t shstrndx;
    uint32_t shstr_size;
    uint32_t shstr_offset;
    uint32_t cursor = 0;
    uint32_t kind;
    uint32_t pass;
    uint32_t i;
    uint32_t j;
    uint32_t flags;
    uint32_t type;
    uint32_t offset;
    uint32_t size;
    uint32_t alignment;
    uint32_t region_start;
    uint32_t symtab_index;
    uint32_t target_index;
    uint32_t symbol_index;
    uint32_t symbol_count;
    uint32_t symbol;
    uint32_t symbol_value;
    uint32_t symbol_section;
    uint32_t place_offset;
    uint32_t place_addr;
    uint32_t exec_start = 0;
    uint32_t exec_end = 0;
    int rc;

    if (elf == 0 || ops == 0 || module == 0 || ops->allocate == 0 ||
        ops->release == 0 || ops->finalize == 0 || ops->invoke == 0) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    canopus_memset(module, 0, sizeof(*module));
    canopus_memset(sections, 0, sizeof(sections));
    if (elf_size < ELF32_EHDR_SIZE || elf[0] != 0x7f || elf[1] != 'E' ||
        elf[2] != 'L' || elf[3] != 'F' || elf[EI_CLASS] != ELFCLASS32 ||
        elf[EI_DATA] != ELFDATA2LSB || u16(elf + 16) != ET_REL ||
        u16(elf + 18) != EM_ARM || u32(elf + 20) != EV_CURRENT ||
        u16(elf + 40) != ELF32_EHDR_SIZE ||
        u16(elf + 46) != ELF32_SHDR_SIZE) {
        return CANOPUS_ELF_LOAD_UNSUPPORTED;
    }
    shoff = u32(elf + 32);
    shnum = u16(elf + 48);
    shstrndx = u16(elf + 50);
    if (shnum == 0 || shnum > CANOPUS_ELF32_MAX_SECTIONS ||
        shstrndx >= shnum || !range_ok(shoff, shnum * ELF32_SHDR_SIZE, elf_size)) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    section = shdr(elf, shoff, shstrndx);
    if (u32(section + 4) != SHT_STRTAB) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    shstr_offset = u32(section + 16);
    shstr_size = u32(section + 20);
    if (!range_ok(shstr_offset, shstr_size, elf_size)) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    shstr = elf + shstr_offset;

    for (pass = 0; pass < 3; pass++) {
        kind = pass == 0 ? CANOPUS_ELF_REGION_EXEC
                         : (pass == 1 ? CANOPUS_ELF_REGION_RO
                                      : CANOPUS_ELF_REGION_RW);
        if (align_up(cursor, 32u, &cursor) != 0) {
            return CANOPUS_ELF_LOAD_INVALID;
        }
        region_start = cursor;
        for (i = 1; i < shnum; i++) {
            section = shdr(elf, shoff, i);
            flags = u32(section + 8);
            type = u32(section + 4);
            if ((flags & SHF_ALLOC) == 0 || section_kind(flags) != kind) {
                continue;
            }
            if ((flags & SHF_WRITE) != 0 && (flags & SHF_EXECINSTR) != 0) {
                return CANOPUS_ELF_LOAD_UNSUPPORTED;
            }
            if (type != SHT_PROGBITS && type != SHT_NOBITS &&
                type != SHT_INIT_ARRAY && type != SHT_FINI_ARRAY &&
                type != SHT_PREINIT_ARRAY && type != SHT_ARM_EXIDX) {
                return CANOPUS_ELF_LOAD_UNSUPPORTED;
            }
            alignment = u32(section + 32);
            if (alignment == 0) {
                alignment = 1;
            }
            if (alignment > 4096u || align_up(cursor, alignment, &cursor) != 0) {
                return CANOPUS_ELF_LOAD_INVALID;
            }
            size = u32(section + 20);
            if (size > UINT32_MAX - cursor) {
                return CANOPUS_ELF_LOAD_INVALID;
            }
            if (type == SHT_PROGBITS &&
                !range_ok(u32(section + 16), size, elf_size)) {
                return CANOPUS_ELF_LOAD_INVALID;
            }
            sections[i].memory_offset = cursor;
            sections[i].kind = kind;
            sections[i].loaded = 1;
            cursor += size;
        }
        if (cursor != region_start) {
            if (align_up(cursor, 32u, &cursor) != 0) {
                return CANOPUS_ELF_LOAD_INVALID;
            }
            module->regions[module->region_count].offset = region_start;
            module->regions[module->region_count].size = cursor - region_start;
            module->regions[module->region_count].kind = kind;
            module->region_count++;
        }
    }
    if (cursor == 0 || align_up(cursor, 32u, &module->allocation_size) != 0) {
        return CANOPUS_ELF_LOAD_INVALID;
    }
    module->allocation = ops->allocate(ops->cookie, module->allocation_size,
                                       32u, &module->target_base);
    if (module->allocation == 0 || (module->target_base & 31u) != 0) {
        release_module(ops, module);
        return CANOPUS_ELF_LOAD_NOMEM;
    }
    memory = (uint8_t *)module->allocation;
    canopus_memset(memory, 0, module->allocation_size);
    for (i = 1; i < shnum; i++) {
        if (!sections[i].loaded) {
            continue;
        }
        section = shdr(elf, shoff, i);
        if (u32(section + 4) != SHT_NOBITS) {
            canopus_memcpy(memory + sections[i].memory_offset,
                           elf + u32(section + 16), u32(section + 20));
        }
    }

    for (i = 1; i < shnum; i++) {
        section = shdr(elf, shoff, i);
        type = u32(section + 4);
        if (type == SHT_RELA) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_UNSUPPORTED;
        }
        if (type != SHT_REL) {
            continue;
        }
        if (u32(section + 36) != ELF32_REL_SIZE ||
            (u32(section + 20) % ELF32_REL_SIZE) != 0) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_INVALID;
        }
        offset = u32(section + 16);
        size = u32(section + 20);
        if (!range_ok(offset, size, elf_size)) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_INVALID;
        }
        symtab_index = u32(section + 24);
        target_index = u32(section + 28);
        if (symtab_index >= shnum || target_index >= shnum ||
            !sections[target_index].loaded) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_INVALID;
        }
        target = shdr(elf, shoff, target_index);
        symtab = shdr(elf, shoff, symtab_index);
        if (u32(symtab + 4) != SHT_SYMTAB ||
            u32(symtab + 36) != ELF32_SYM_SIZE ||
            (u32(symtab + 20) % ELF32_SYM_SIZE) != 0 ||
            !range_ok(u32(symtab + 16), u32(symtab + 20), elf_size)) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_INVALID;
        }
        symbol_count = u32(symtab + 20) / ELF32_SYM_SIZE;
        for (j = 0; j < size; j += ELF32_REL_SIZE) {
            rel = elf + offset + j;
            place_offset = u32(rel);
            symbol_index = u32(rel + 4) >> 8;
            type = u32(rel + 4) & 0xffu;
            if (symbol_index >= symbol_count ||
                place_offset > u32(target + 20) ||
                4u > u32(target + 20) - place_offset) {
                release_module(ops, module);
                return CANOPUS_ELF_LOAD_INVALID;
            }
            symbol_entry = elf + u32(symtab + 16) +
                           symbol_index * ELF32_SYM_SIZE;
            symbol_value = u32(symbol_entry + 4);
            symbol_section = u16(symbol_entry + 14);
            if (symbol_section == SHN_ABS) {
                symbol = symbol_value;
            } else if (symbol_section == SHN_UNDEF ||
                       symbol_section == SHN_COMMON ||
                       symbol_section >= shnum ||
                       !sections[symbol_section].loaded) {
                release_module(ops, module);
                return CANOPUS_ELF_LOAD_UNSUPPORTED;
            } else {
                if (symbol_value > u32(shdr(elf, shoff, symbol_section) + 20)) {
                    release_module(ops, module);
                    return CANOPUS_ELF_LOAD_INVALID;
                }
                symbol = module->target_base + sections[symbol_section].memory_offset +
                         symbol_value;
            }
            place_addr = module->target_base + sections[target_index].memory_offset +
                         place_offset;
            rc = canopus_arm_rel_apply(type,
                                       memory + sections[target_index].memory_offset +
                                           place_offset,
                                       symbol, place_addr);
            if (rc != CANOPUS_RELOC_OK) {
                release_module(ops, module);
                return CANOPUS_ELF_LOAD_RELOCATION;
            }
        }
    }

    if (module->region_count == 0 ||
        module->regions[0].kind != CANOPUS_ELF_REGION_EXEC) {
        release_module(ops, module);
        return CANOPUS_ELF_LOAD_CONSTRUCTORS;
    }
    exec_start = module->target_base + module->regions[0].offset;
    exec_end = exec_start + module->regions[0].size;
    for (i = 1; i < shnum; i++) {
        uint32_t name;
        if (!sections[i].loaded) {
            continue;
        }
        section = shdr(elf, shoff, i);
        name = u32(section);
        if (name_equals(shstr, shstr_size, name, ".preinit_array")) {
            rc = append_array(module->preinit, &module->preinit_count,
                              memory + sections[i].memory_offset,
                              u32(section + 20), exec_start, exec_end);
        } else if (name_equals(shstr, shstr_size, name, ".init_array")) {
            rc = append_array(module->init, &module->init_count,
                              memory + sections[i].memory_offset,
                              u32(section + 20), exec_start, exec_end);
        } else if (name_equals(shstr, shstr_size, name, ".fini_array")) {
            rc = append_array(module->fini, &module->fini_count,
                              memory + sections[i].memory_offset,
                              u32(section + 20), exec_start, exec_end);
        } else {
            rc = CANOPUS_ELF_LOAD_OK;
        }
        if (rc != CANOPUS_ELF_LOAD_OK) {
            release_module(ops, module);
            return rc;
        }
    }
    if (ops->finalize(ops->cookie, module->allocation, module->target_base,
                      module->allocation_size, module->regions,
                      module->region_count) != 0) {
        release_module(ops, module);
        return CANOPUS_ELF_LOAD_FINALIZE;
    }
    for (i = 0; i < module->preinit_count; i++) {
        if (ops->invoke(ops->cookie, module->preinit[i]) != 0) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_CONSTRUCTORS;
        }
    }
    for (i = 0; i < module->init_count; i++) {
        if (ops->invoke(ops->cookie, module->init[i]) != 0) {
            release_module(ops, module);
            return CANOPUS_ELF_LOAD_CONSTRUCTORS;
        }
    }
    return CANOPUS_ELF_LOAD_OK;
}

void canopus_elf32_unload(const struct canopus_elf_loader_ops *ops,
                          struct canopus_elf_module *module)
{
    uint32_t i;

    if (ops == 0 || module == 0) {
        return;
    }
    i = module->fini_count;
    while (i != 0) {
        i--;
        if (ops->invoke != 0) {
            (void)ops->invoke(ops->cookie, module->fini[i]);
        }
    }
    release_module(ops, module);
}
