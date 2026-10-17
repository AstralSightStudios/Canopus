/*
 * canopus_hook.c — RAM pointer redirection and conservative Thumb trampolines.
 * Flash functions can only be redirected through developer-registered RAM slots;
 * this engine never writes Flash or programs breakpoint/patch hardware.
 */
#include "canopus_hook.h"
#include "canopus_memory.h"

struct hook_entry {
    int used;
    uint32_t slot;
    uintptr_t target_fn;
    uintptr_t hook_slot_addr; /* Physical RAM slot, for both direct and mapped hooks. */
    uintptr_t orig_fn;
    canopus_hook_type_t type;
    uint8_t orig_bytes[16];
    uint8_t orig_len;
};

static struct hook_entry s_hooks[CANOPUS_HOOK_MAX_SLOTS];

struct canopus_flash_slot_map {
    uint32_t slot; /* Owning module; only that module may use this mapping. */
    uintptr_t flash_fn;
    uintptr_t ram_slot;
};

#define CANOPUS_MAX_DYNAMIC_SLOT_MAPS 16u
static struct canopus_flash_slot_map s_dynamic_slot_maps[CANOPUS_MAX_DYNAMIC_SLOT_MAPS];
static uint32_t s_dynamic_slot_map_count;

static uintptr_t function_address(uintptr_t fn)
{
    return fn & ~(uintptr_t)1u;
}

/* Address regions, not a firmware function/dispatch table. */
static int is_flash_address(uintptr_t addr)
{
    return (addr >= 0x0C000000u && addr < 0x0E000000u) ||
           (addr >= 0x1C000000u && addr < 0x1E000000u);
}

static int is_ram_slot(uintptr_t addr)
{
    if (addr == 0 || (addr & (sizeof(uintptr_t) - 1u)) != 0u) return 0;
#ifdef CANOPUS_HOST
    return !is_flash_address(addr);
#else
    /* Only the architectural SRAM region is writable by this engine. */
    return addr >= 0x20000000u && addr < 0x40000000u;
#endif
}

static uintptr_t find_ram_slot_for_flash(uint32_t slot, uintptr_t flash_fn)
{
    uint32_t i;
    uintptr_t even = function_address(flash_fn);
    for (i = 0u; i < s_dynamic_slot_map_count; i++) {
        if (s_dynamic_slot_maps[i].slot == slot &&
            s_dynamic_slot_maps[i].flash_fn == even) {
            return s_dynamic_slot_maps[i].ram_slot;
        }
    }
    return 0;
}

static int ram_slot_is_hooked(uintptr_t ram_slot)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (s_hooks[i].used && s_hooks[i].hook_slot_addr == ram_slot) return 1;
    }
    return 0;
}

int32_t canopus_hook_register_slot_map(uint32_t slot, uintptr_t flash_fn,
                                     uintptr_t ram_slot)
{
    uint32_t i;
    uintptr_t even = function_address(flash_fn);
    if (even == 0 || !is_ram_slot(ram_slot)) return CANOPUS_SVC_ERR_ARGUMENT;
    for (i = 0u; i < s_dynamic_slot_map_count; i++) {
        struct canopus_flash_slot_map *map = &s_dynamic_slot_maps[i];
        if (map->slot == slot && map->flash_fn == even) {
            if (map->ram_slot == ram_slot) return CANOPUS_SVC_OK;
            if (canopus_hook_is_hooked(flash_fn)) return CANOPUS_SVC_ERR_STATE;
            map->ram_slot = ram_slot;
            return CANOPUS_SVC_OK;
        }
    }
    if (s_dynamic_slot_map_count >= CANOPUS_MAX_DYNAMIC_SLOT_MAPS) {
        return CANOPUS_SVC_ERR_NOMEM;
    }
    s_dynamic_slot_maps[s_dynamic_slot_map_count].slot = slot;
    s_dynamic_slot_maps[s_dynamic_slot_map_count].flash_fn = even;
    s_dynamic_slot_maps[s_dynamic_slot_map_count].ram_slot = ram_slot;
    s_dynamic_slot_map_count++;
    return CANOPUS_SVC_OK;
}

#ifndef CANOPUS_HOST

__attribute__((aligned(4)))
static uint8_t s_trampolines[CANOPUS_HOOK_MAX_SLOTS][32];

static void sync_cache(const void *addr, size_t len)
{
    (void)addr;
    (void)len;
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

static uint32_t thumb_insn_len(uint16_t w)
{
    return (w & 0xF800u) >= 0xE800u ? 4u : 2u;
}

/* This is a copy-safe allowlist, not a relocator/disassembler. Unknown opcodes,
 * all control flow, PC operands and IT blocks are explicitly unsupported. In
 * particular, copying an IT before inserting our jump could predicate that jump
 * or leave its remaining instructions outside the trampoline. */
static int thumb_insn_copy_safe(uint16_t w1, uint16_t w2)
{
    if (thumb_insn_len(w1) == 4u) {
        /* MOVW / MOVT, with a general-purpose destination (not SP or PC). */
        if ((w1 & 0xFBF0u) == 0xF240u || (w1 & 0xFBF0u) == 0xF2C0u) {
            uint32_t rd = (w2 >> 8) & 0xFu;
            return (w2 & 0x8000u) == 0u && rd != 13u && rd != 15u;
        }
        /* PUSH.W, excluding SP and PC in the register list. */
        if (w1 == 0xE92Du) {
            return (w2 & 0xA000u) == 0u && w2 != 0u &&
                   (w2 & (w2 - 1u)) != 0u;
        }
        return 0;
    }

    /* Shift/add/sub/immediate arithmetic and low-register data processing. */
    if (w1 < 0x4400u) return 1;
    /* High-register ADD/CMP/MOV: neither operand may be PC. BX/BLX excluded. */
    if (w1 < 0x4700u) {
        uint32_t rd = (w1 & 7u) | ((w1 >> 4) & 8u);
        uint32_t rm = (w1 >> 3) & 15u;
        return rd != 15u && rm != 15u;
    }
    /* Register/immediate load-store and SP-relative load-store (low registers). */
    if (w1 >= 0x5000u && w1 < 0xA000u) return 1;
    /* ADD Rd, SP, #imm; ADD/SUB SP, #imm. ADR (PC-relative) is excluded. */
    if ((w1 & 0xF800u) == 0xA800u || (w1 & 0xFF00u) == 0xB000u) return 1;
    /* Sign/zero extend, PUSH (low registers + optional LR), POP without PC. */
    if ((w1 & 0xFF00u) == 0xB200u) return 1;
    if ((w1 & 0xFE00u) == 0xB400u) return (w1 & 0x01FFu) != 0u;
    if ((w1 & 0xFF00u) == 0xBC00u) return (w1 & 0x00FFu) != 0u;
    /* REV, REV16, REVSH; NOP (not IT or other hints). */
    if ((w1 & 0xFFC0u) == 0xBA00u || (w1 & 0xFFC0u) == 0xBA40u ||
        (w1 & 0xFFC0u) == 0xBAC0u || w1 == 0xBF00u) return 1;
    return 0;
}

static uint32_t thumb_copy_len(uintptr_t target, uint32_t min_len)
{
    const uint16_t *insns = (const uint16_t *)target;
    uint32_t len = 0u;
    while (len < min_len) {
        uint16_t w1 = insns[len / 2u];
        uint32_t width = thumb_insn_len(w1);
        uint16_t w2 = width == 4u ? insns[len / 2u + 1u] : 0u;
        if (!thumb_insn_copy_safe(w1, w2)) return 0u;
        len += width;
    }
    return len;
}

#endif /* !CANOPUS_HOST */

void canopus_hook_engine_init(void)
{
    canopus_memset(s_hooks, 0, sizeof(s_hooks));
    canopus_memset(s_dynamic_slot_maps, 0, sizeof(s_dynamic_slot_maps));
    s_dynamic_slot_map_count = 0u;
}

static int alloc_hook_slot(void)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (!s_hooks[i].used) return (int)i;
    }
    return -1;
}

int32_t canopus_hook_install(uint32_t slot,
                             uintptr_t target_fn,
                             uintptr_t hook_fn,
                             uintptr_t *orig_fn,
                             canopus_hook_type_t hook_type)
{
    int id;
    struct hook_entry *h;
    uintptr_t target_even = function_address(target_fn);
    uintptr_t ram_slot;

    if (target_even == 0 || hook_fn == 0 || orig_fn == 0 ||
        hook_type < CANOPUS_HOOK_AUTO || hook_type > CANOPUS_HOOK_POINTER) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (canopus_hook_is_hooked(target_fn)) return CANOPUS_SVC_ERR_STATE;

    ram_slot = find_ram_slot_for_flash(slot, target_fn);
    if (ram_slot == 0) {
        if (is_flash_address(target_even)) return CANOPUS_SVC_ERR_UNSUPPORTED;
        if (hook_type == CANOPUS_HOOK_AUTO) {
#ifdef CANOPUS_HOST
            /* Native function and data pointers have no Thumb tag to distinguish them. */
            return CANOPUS_SVC_ERR_UNSUPPORTED;
#else
            hook_type = (target_fn & (sizeof(uintptr_t) - 1u)) == 0u ?
                        CANOPUS_HOOK_POINTER : CANOPUS_HOOK_INLINE;
#endif
        }
        if (hook_type == CANOPUS_HOOK_POINTER) {
            if (!is_ram_slot(target_fn)) return CANOPUS_SVC_ERR_ARGUMENT;
            ram_slot = target_fn;
        } else if (hook_type != CANOPUS_HOOK_INLINE) {
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }
    }
    if (ram_slot != 0 && ram_slot_is_hooked(ram_slot)) return CANOPUS_SVC_ERR_STATE;

    id = alloc_hook_slot();
    if (id < 0) return CANOPUS_SVC_ERR_NOMEM;
    h = &s_hooks[id];

    if (ram_slot != 0) {
        uintptr_t prev = *(volatile uintptr_t *)ram_slot;
        h->orig_fn = prev; /* NULL is an original value too; never synthesize one. */
        h->hook_slot_addr = ram_slot;
        h->type = CANOPUS_HOOK_POINTER;
        *orig_fn = prev;
        *(volatile uintptr_t *)ram_slot = hook_fn;
    } else {
#ifdef CANOPUS_HOST
        /* A native host cannot execute Thumb trampolines. Do not fake success. */
        return CANOPUS_SVC_ERR_UNSUPPORTED;
#else
        uint8_t *code = (uint8_t *)target_even;
        uint8_t *orig_buf = s_trampolines[id];
        uint32_t jmp_offset = (target_even & 2u) ? 2u : 0u;
        uint32_t copy_len;
        uint32_t orig_pos;
        uint32_t resume_target;
        uint32_t hook_target;
        uint32_t p;
        if (target_even < 0x20000000u || target_even >= 0x40000000u) {
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }
        copy_len = thumb_copy_len(target_even, jmp_offset + 8u);
        if (copy_len == 0u || copy_len > sizeof(h->orig_bytes)) {
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }
        canopus_memcpy(h->orig_bytes, code, copy_len);
        h->orig_len = (uint8_t)copy_len;
        canopus_memcpy(orig_buf, code, copy_len);
        orig_pos = copy_len;
        if ((orig_pos & 2u) != 0u) {
            orig_buf[orig_pos++] = 0x00;
            orig_buf[orig_pos++] = 0xBF; /* NOP to word-align LDR.W PC. */
        }
        orig_buf[orig_pos + 0u] = 0xDF;
        orig_buf[orig_pos + 1u] = 0xF8;
        orig_buf[orig_pos + 2u] = 0x00;
        orig_buf[orig_pos + 3u] = 0xF0;
        resume_target = (uint32_t)((target_even + copy_len) | 1u);
        canopus_memcpy(&orig_buf[orig_pos + 4u], &resume_target, 4u);
        sync_cache(orig_buf, orig_pos + 8u);
        h->orig_fn = (uintptr_t)orig_buf | 1u;
        h->type = CANOPUS_HOOK_INLINE;

        if (jmp_offset != 0u) {
            code[0] = 0x00;
            code[1] = 0xBF;
        }
        code[jmp_offset + 0u] = 0xDF;
        code[jmp_offset + 1u] = 0xF8;
        code[jmp_offset + 2u] = 0x00;
        code[jmp_offset + 3u] = 0xF0;
        hook_target = (uint32_t)(hook_fn | 1u);
        canopus_memcpy(&code[jmp_offset + 4u], &hook_target, 4u);
        for (p = jmp_offset + 8u; p < copy_len; p += 2u) {
            code[p] = 0x00;
            code[p + 1u] = 0xBF;
        }
        sync_cache(code, copy_len);
        *orig_fn = h->orig_fn;
#endif
    }
    h->slot = slot;
    h->target_fn = target_fn;
    h->used = 1;
    return id;
}

int32_t canopus_hook_uninstall(uint32_t slot, int32_t hook_id)
{
    struct hook_entry *h;
    if (hook_id < 0 || hook_id >= (int32_t)CANOPUS_HOOK_MAX_SLOTS) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    h = &s_hooks[hook_id];
    if (!h->used || h->slot != slot) return CANOPUS_SVC_ERR_NOTFOUND;
    if (h->type == CANOPUS_HOOK_POINTER) {
        *(volatile uintptr_t *)h->hook_slot_addr = h->orig_fn;
    }
#ifndef CANOPUS_HOST
    else if (h->type == CANOPUS_HOOK_INLINE) {
        uint8_t *code = (uint8_t *)function_address(h->target_fn);
        canopus_memcpy(code, h->orig_bytes, h->orig_len);
        sync_cache(code, h->orig_len);
    }
#endif
    canopus_memset(h, 0, sizeof(*h));
    return CANOPUS_SVC_OK;
}

void canopus_hook_release_slot(uint32_t slot)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (s_hooks[i].used && s_hooks[i].slot == slot) {
            (void)canopus_hook_uninstall(slot, (int32_t)i);
        }
    }
    /* Restore before discarding mappings; the owner may free its RAM next. */
    i = 0u;
    while (i < s_dynamic_slot_map_count) {
        if (s_dynamic_slot_maps[i].slot == slot) {
            s_dynamic_slot_map_count--;
            s_dynamic_slot_maps[i] = s_dynamic_slot_maps[s_dynamic_slot_map_count];
            canopus_memset(&s_dynamic_slot_maps[s_dynamic_slot_map_count], 0,
                           sizeof(s_dynamic_slot_maps[0]));
        } else {
            i++;
        }
    }
}

int canopus_hook_is_active(int32_t hook_id)
{
    if (hook_id < 0 || hook_id >= (int32_t)CANOPUS_HOOK_MAX_SLOTS) return 0;
    return s_hooks[hook_id].used;
}

int canopus_hook_is_hooked(uintptr_t target_fn)
{
    uint32_t i;
    uintptr_t even = function_address(target_fn);
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (s_hooks[i].used &&
            (function_address(s_hooks[i].target_fn) == even ||
             s_hooks[i].hook_slot_addr == target_fn)) return 1;
    }
    /* Queries also recognize other registered names for an occupied RAM slot. */
    for (i = 0u; i < s_dynamic_slot_map_count; i++) {
        if (s_dynamic_slot_maps[i].flash_fn == even &&
            ram_slot_is_hooked(s_dynamic_slot_maps[i].ram_slot)) return 1;
    }
    return 0;
}
