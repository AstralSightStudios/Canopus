/*
 * canopus_hook.c — Universal dynamic function hooking for Canopus on Cortex-M33.
 *
 * Supports three hooking mechanisms:
 *   1. CANOPUS_HOOK_POINTER: Atomic replacement of function pointer in RAM tables
 *                            (e.g., VFS open_cb, driver ops, vtables). Preferred,
 *                            safe, hardware-independent.
 *   2. CANOPUS_HOOK_INLINE : Thumb-2 8-byte LDR.W PC trampoline for SRAM functions.
 *                            Guarded by PC-relative check and function boundary check.
 *   3. CANOPUS_HOOK_FPB    : Direct instruction patch fallback (for emulators
 *                            where Flash memory is writable). On Cortex-M33 real
 *                            silicon without hardware Flash patch (FP_REMAP is
 *                            RAZ/WI per TRM §13.1), Flash functions are hooked
 *                            via RAM dispatch slot redirection (Corona-style).
 */
#include "canopus_hook.h"
#include "canopus_memory.h"

struct hook_entry {
    int used;
    uint32_t slot;
    uintptr_t target_fn;
    uintptr_t hook_slot_addr; /* RAM slot address if redirected via pointer slot */
    uintptr_t hook_fn;
    uintptr_t orig_fn;
    canopus_hook_type_t type;
    int fpb_comp;
    uint8_t orig_bytes[16];
    uint8_t orig_len;
    uint8_t trampoline[32];
};

static struct hook_entry s_hooks[CANOPUS_HOOK_MAX_SLOTS];

/* -------------------------------------------------------------------------- */
/* FLASH-TO-RAM DISPATCH SLOT MAPPINGS (Corona-style RAM Slot Hooking)        */
/* -------------------------------------------------------------------------- */

struct canopus_flash_slot_map {
    uintptr_t flash_fn;  /* Flash function entry address */
    uintptr_t ram_slot;  /* RAM dispatch / vtable / driver slot address */
};

#define CANOPUS_MAX_DYNAMIC_SLOT_MAPS 16u
static struct canopus_flash_slot_map s_dynamic_slot_maps[CANOPUS_MAX_DYNAMIC_SLOT_MAPS];
static uint32_t s_dynamic_slot_map_count = 0u;

int32_t canopus_hook_register_slot_map(uintptr_t flash_fn, uintptr_t ram_slot)
{
    uint32_t i;
    uintptr_t flash_even;
    if (flash_fn == 0 || ram_slot == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    flash_even = flash_fn & ~1u;
    for (i = 0u; i < s_dynamic_slot_map_count; i++) {
        if ((s_dynamic_slot_maps[i].flash_fn & ~1u) == flash_even) {
            s_dynamic_slot_maps[i].ram_slot = ram_slot;
            return CANOPUS_SVC_OK;
        }
    }
    if (s_dynamic_slot_map_count >= CANOPUS_MAX_DYNAMIC_SLOT_MAPS) {
        return CANOPUS_SVC_ERR_NOMEM;
    }
    s_dynamic_slot_maps[s_dynamic_slot_map_count].flash_fn = flash_even;
    s_dynamic_slot_maps[s_dynamic_slot_map_count].ram_slot = ram_slot;
    s_dynamic_slot_map_count++;
    return CANOPUS_SVC_OK;
}

static uintptr_t find_ram_slot_for_flash(uintptr_t flash_fn)
{
    uint32_t i;
    uintptr_t flash_even = flash_fn & ~1u;
    for (i = 0u; i < s_dynamic_slot_map_count; i++) {
        if ((s_dynamic_slot_maps[i].flash_fn & ~1u) == flash_even) {
            return s_dynamic_slot_maps[i].ram_slot;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------- */
/* THUMB-2 INSTRUCTION ANALYSIS                                               */
/* -------------------------------------------------------------------------- */

static inline __attribute__((unused)) uint32_t thumb_insn_len(uint16_t w)
{
    return ((w & 0xF800u) == 0xE800u || (w & 0xF000u) == 0xF000u) ? 4u : 2u;
}

/* Returns 1 if instruction is PC-relative (cannot be safely copied to SRAM trampoline) */
static __attribute__((unused)) int is_pcrel_insn(uint16_t w1, uint16_t w2)
{
    /* 16-bit Thumb: */
    /* LDR <Rt>, [PC, #imm8]: 0100 1xxx xxxx xxxx */
    if ((w1 & 0xF800u) == 0x4800u) return 1;
    /* ADR <Rd>, <label>: ADD <Rd>, PC, #imm8: 1010 0xxx xxxx xxxx */
    if ((w1 & 0xF800u) == 0xA000u) return 1;
    /* B<cond> <label>: 1101 xxxx xxxx xxxx (cond != 1110/1111: exclude UDF 0xDE.., SVC 0xDF..) */
    if ((w1 & 0xF000u) == 0xD000u && (w1 & 0x0E00u) != 0x0E00u) return 1;
    /* B <label> (unconditional): 1110 0xxx xxxx xxxx */
    if ((w1 & 0xF800u) == 0xE000u) return 1;
    /* CBZ / CBNZ: 1011 x0x1 xxxx xxxx */
    if ((w1 & 0xF500u) == 0xB100u) return 1;

    /* 32-bit Thumb-2: */
    if ((w1 & 0xF800u) == 0xE800u || (w1 & 0xF000u) == 0xF000u) {
        /* B.W, B<cond>.W, BL, BLX: 1111 0xxx xxxx xxxx with bit 15 of w2 set */
        if ((w1 & 0xF800u) == 0xF000u && (w2 & 0x8000u) == 0x8000u) return 1;
        /* ADR.W (add/sub PC): 1111 0x10 0x0x 1111 */
        if ((w1 & 0xFBFFu) == 0xF20Fu || (w1 & 0xFBFFu) == 0xF2AFu) return 1;
        /* LDR.W literal / PC-relative (Rn == 15): 1111 100x xx01 1111 */
        if ((w1 & 0xFE5Fu) == 0xF85Fu && (w1 & 0x000Fu) == 0x000Fu) return 1;
        /* TBB / TBH [PC, Rm]: 1110 1000 1101 1111 */
        if ((w1 & 0xFFFFu) == 0xE8DFu) return 1;
    }
    return 0;
}

/* Returns 1 if instruction is an unconditional function exit/return */
static __attribute__((unused)) int is_func_exit_insn(uint16_t w1, uint16_t w2)
{
    /* 16-bit BX <Rm>: 0100 0111 0xxx x000 (includes BX LR = 0x4770) */
    if ((w1 & 0xFF87u) == 0x4700u) return 1;
    /* 16-bit POP {..., PC}: 1011 1101 xxxx xxxx (Bit 8 is P: 1 = PC popped) */
    if ((w1 & 0xFF00u) == 0xBD00u) return 1;
    /* 16-bit unconditional B <label>: 1110 0xxx xxxx xxxx */
    if ((w1 & 0xF800u) == 0xE000u) return 1;

    /* 32-bit Thumb-2: */
    if ((w1 & 0xF800u) == 0xE800u || (w1 & 0xF000u) == 0xF000u) {
        /* POP.W / LDMIA.W SP!, {..., PC}: 1110 1000 1011 1101 with bit 15 of w2 set */
        if ((w1 & 0xFFFFu) == 0xE8BDu && (w2 & 0x8000u) != 0) return 1;
        /* LDR.W PC, [SP], #imm: 1111 1000 0101 1101 with Rt = 15 (PC) */
        if ((w1 & 0xFFFFu) == 0xF85Du && (w2 & 0xF000u) == 0xF000u) return 1;
        /* Unconditional B.W: 1111 0xxx xxxx xxxx with 10x1 in w2 */
        if ((w1 & 0xF800u) == 0xF000u && (w2 & 0xD000u) == 0x9000u) return 1;
    }
    return 0;
}

#ifndef CANOPUS_HOST

/* Hardware Cortex-M33 FPB registers */
#define FPB_CTRL        (*(volatile uint32_t *)(uintptr_t)0xE0002000u)
#define FPB_REMAP       (*(volatile uint32_t *)(uintptr_t)0xE0002004u)
#define FPB_COMP(n)     (*(volatile uint32_t *)(uintptr_t)(0xE0002008u + (uint32_t)(n) * 4u))

#define FPB_KEY         (1u << 1)
#define FPB_ENABLE      (1u << 0)

/* Remap table must be 64-byte aligned in SRAM */
__attribute__((aligned(64)))
static uint32_t s_fpb_remap_table[CANOPUS_FPB_MAX_COMPS];
static uint8_t s_fpb_comp_used[CANOPUS_FPB_MAX_COMPS];
static int s_has_hw_fpb = 0;

__attribute__((aligned(4)))
static uint8_t s_trampolines[CANOPUS_HOOK_MAX_SLOTS][32];

static inline void sync_cache(const void *addr, size_t len)
{
    (void)addr;
    (void)len;
    /* Cortex-M33 Memory barrier: DSB ensures all writes complete; ISB flushes processor pipeline */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
}

static inline void emit_thumb2_movw_r12(uint16_t *out, uint16_t imm16)
{
    uint16_t imm4 = (imm16 >> 12) & 0x0Fu;
    uint16_t i    = (imm16 >> 11) & 0x01u;
    uint16_t imm3 = (imm16 >> 8)  & 0x07u;
    uint16_t imm8 = imm16 & 0xFFu;
    out[0] = (uint16_t)(0xF240u | (i << 10) | imm4);
    out[1] = (uint16_t)((imm3 << 12) | (12u << 8) | imm8);
}

static inline void emit_thumb2_movt_r12(uint16_t *out, uint16_t imm16)
{
    uint16_t imm4 = (imm16 >> 12) & 0x0Fu;
    uint16_t i    = (imm16 >> 11) & 0x01u;
    uint16_t imm3 = (imm16 >> 8)  & 0x07u;
    uint16_t imm8 = imm16 & 0xFFu;
    out[0] = (uint16_t)(0xF2C0u | (i << 10) | imm4);
    out[1] = (uint16_t)((imm3 << 12) | (12u << 8) | imm8);
}

static inline void emit_thumb2_bx_r12_nop(uint16_t *out)
{
    out[0] = 0x4760u;
    out[1] = 0xBF00u;
}

#endif /* !CANOPUS_HOST */

void canopus_hook_engine_init(void)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        canopus_memset(&s_hooks[i], 0, sizeof(s_hooks[i]));
    }
    s_dynamic_slot_map_count = 0u;
#ifndef CANOPUS_HOST
    for (i = 0u; i < CANOPUS_FPB_MAX_COMPS; i++) {
        s_fpb_comp_used[i] = 0u;
        FPB_COMP(i) = 0u;
    }
    /* Note: Cortex-M33 BPU does NOT implement FP_REMAP (RAZ/WI per TRM §13.1).
     * Hardware remap is disabled; Flash functions are hooked via RAM dispatch slots. */
    s_has_hw_fpb = 0;
#endif
}

static int alloc_hook_slot(void)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (!s_hooks[i].used) return (int)i;
    }
    return -1;
}

#ifndef CANOPUS_HOST
static int alloc_fpb_comps(uint32_t count)
{
    uint32_t i, j;
    if (count == 0 || count > CANOPUS_FPB_MAX_COMPS) return -1;
    for (i = 0u; i + count <= CANOPUS_FPB_MAX_COMPS; i++) {
        int ok = 1;
        for (j = 0u; j < count; j++) {
            if (s_fpb_comp_used[i + j]) { ok = 0; break; }
        }
        if (ok) {
            for (j = 0u; j < count; j++) s_fpb_comp_used[i + j] = 1u;
            return (int)i;
        }
    }
    return -1;
}
#endif

int32_t canopus_hook_install(uint32_t slot,
                             uintptr_t target_fn,
                             uintptr_t hook_fn,
                             uintptr_t *orig_fn,
                             canopus_hook_type_t hook_type)
{
    int id;
    struct hook_entry *h;
    uintptr_t target_even;

    if (target_fn == 0 || hook_fn == 0 || orig_fn == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }

    /* Prevent repeated hooking of the same target to preserve restore chain */
    if (canopus_hook_is_hooked(target_fn)) {
        return CANOPUS_SVC_ERR_STATE;
    }

    id = alloc_hook_slot();
    if (id < 0) return CANOPUS_SVC_ERR_NOMEM;

    h = &s_hooks[id];
    h->used = 1;
    h->slot = slot;
    h->target_fn = target_fn;
    h->hook_slot_addr = 0;
    h->hook_fn = hook_fn;
    h->type = hook_type;
    h->fpb_comp = -1;

    target_even = target_fn & ~1u;

#ifdef CANOPUS_HOST
    (void)target_even;
    uintptr_t ram_slot = find_ram_slot_for_flash(target_even);
    if (ram_slot != 0) {
        uintptr_t prev = *(uintptr_t *)ram_slot;
        *(uintptr_t *)ram_slot = hook_fn;
        *orig_fn = prev ? prev : target_fn;
        h->orig_fn = *orig_fn;
        h->hook_slot_addr = ram_slot;
        h->type = CANOPUS_HOOK_POINTER;
        return id;
    }
    /* Host testing simulation: safe pointer substitution or mock trampoline */
    if (hook_type == CANOPUS_HOOK_POINTER) {
        *orig_fn = *(uintptr_t *)target_fn;
        *(uintptr_t *)target_fn = hook_fn;
        h->type = CANOPUS_HOOK_POINTER;
    } else {
        *orig_fn = target_fn;
        h->type = (hook_type == CANOPUS_HOOK_AUTO) ? CANOPUS_HOOK_INLINE : hook_type;
    }
    h->orig_fn = *orig_fn;
    return id;
#else

    /* 1. FLASH FUNCTION HOOKING */
    int is_flash = ((target_even >= 0x0C000000u && target_even < 0x0E000000u) ||
                    (target_even >= 0x1C000000u && target_even < 0x1E000000u));

    if (is_flash) {
        /* Check if a known/registered RAM pointer slot exists (Corona-style RAM Slot Hooking).
         * On real Cortex-M33 hardware, this redirects callers invoking the Flash function
         * through the firmware's RAM dispatch table or vtable. */
        uintptr_t ram_slot = find_ram_slot_for_flash(target_even);
        if (ram_slot != 0) {
            uintptr_t prev = *(volatile uintptr_t *)ram_slot;
            *(volatile uintptr_t *)ram_slot = hook_fn;
            *orig_fn = prev ? prev : target_fn;
            h->orig_fn = *orig_fn;
            h->hook_slot_addr = ram_slot;
            h->type = CANOPUS_HOOK_POINTER;
            return id;
        }

        /* If caller explicitly requested POINTER hook on Flash without a slot, reject */
        if (hook_type == CANOPUS_HOOK_POINTER) {
            h->used = 0;
            return CANOPUS_SVC_ERR_ARGUMENT;
        }

        /* Direct patch fallback (for emulators where Flash memory is writable) */
        uint8_t *orig_buf = &s_trampolines[id][0];
        const uint16_t *insns = (const uint16_t *)target_even;
        uint32_t copy_len = 0u;
        uint32_t orig_pos;
        int comp;

        /* Decode instructions at target to cover at least 12 bytes (3 words) */
        while (copy_len < 12u) {
            uint16_t w = insns[copy_len / 2u];
            uint16_t w_next = insns[(copy_len / 2u) + 1u];
            if (is_func_exit_insn(w, w_next)) {
                h->used = 0;
                return CANOPUS_SVC_ERR_UNSUPPORTED; /* function too short */
            }
            if (is_pcrel_insn(w, w_next)) {
                h->used = 0;
                return CANOPUS_SVC_ERR_UNSUPPORTED; /* contains PC-relative instruction */
            }
            copy_len += thumb_insn_len(w);
        }

        if (copy_len > sizeof(h->orig_bytes)) {
            h->used = 0;
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }

        canopus_memcpy(h->orig_bytes, (const void *)target_even, copy_len);
        h->orig_len = (uint8_t)copy_len;

        /* Trampoline in SRAM: execute original instructions, then jump back to target_even + copy_len */
        canopus_memcpy(orig_buf, (const void *)target_even, copy_len);
        orig_pos = copy_len;
        if ((orig_pos & 2u) != 0u) {
            orig_buf[orig_pos] = 0x00;
            orig_buf[orig_pos + 1] = 0xBF; /* NOP */
            orig_pos += 2u;
        }
        orig_buf[orig_pos + 0] = 0xDF;
        orig_buf[orig_pos + 1] = 0xF8;
        orig_buf[orig_pos + 2] = 0x00;
        orig_buf[orig_pos + 3] = 0xF0;
        *(uint32_t *)&orig_buf[orig_pos + 4] = (uint32_t)((target_even + copy_len) | 1u);
        sync_cache(orig_buf, orig_pos + 8u);
        *orig_fn = (uintptr_t)orig_buf | 1u;
        h->orig_fn = *orig_fn;

        comp = alloc_fpb_comps(3);
        if (comp < 0 && s_has_hw_fpb) {
            h->used = 0;
            return CANOPUS_SVC_ERR_STATE; /* no FPB slots */
        }
        h->fpb_comp = comp;

        if (comp >= 0 && s_has_hw_fpb) {
            emit_thumb2_movw_r12((uint16_t *)&s_fpb_remap_table[comp + 0], (uint16_t)(hook_fn & 0xFFFFu));
            FPB_COMP(comp + 0) = (((uint32_t)target_even + 0u) & 0x1FFFFFFEu) | 1u;

            emit_thumb2_movt_r12((uint16_t *)&s_fpb_remap_table[comp + 1], (uint16_t)((hook_fn >> 16) & 0xFFFFu));
            FPB_COMP(comp + 1) = (((uint32_t)target_even + 4u) & 0x1FFFFFFEu) | 1u;

            emit_thumb2_bx_r12_nop((uint16_t *)&s_fpb_remap_table[comp + 2]);
            FPB_COMP(comp + 2) = (((uint32_t)target_even + 8u) & 0x1FFFFFFEu) | 1u;

            sync_cache((void *)&s_fpb_remap_table[comp], 12);
            sync_cache((void *)&FPB_COMP(comp), 12);
        }

        if (!s_has_hw_fpb) {
            /* In emulator environments where Flash memory alias is writable: */
            uint16_t *code16 = (uint16_t *)target_even;
            emit_thumb2_movw_r12(&code16[0], (uint16_t)(hook_fn & 0xFFFFu));
            emit_thumb2_movt_r12(&code16[2], (uint16_t)((hook_fn >> 16) & 0xFFFFu));
            emit_thumb2_bx_r12_nop(&code16[4]);
            for (uint32_t p = 6u; p < copy_len / 2u; p++) {
                code16[p] = 0xBF00u; /* NOP */
            }
            sync_cache(code16, copy_len);
        }
        h->type = CANOPUS_HOOK_FPB;
        return id;
    }

    /* 2. RAM POINTER SLOT HOOKING */
    if (hook_type == CANOPUS_HOOK_AUTO) {
        if ((target_fn & 1u) == 0 && (target_fn & 3u) == 0) {
            hook_type = CANOPUS_HOOK_POINTER;
        } else {
            hook_type = CANOPUS_HOOK_INLINE;
        }
    }

    if (hook_type == CANOPUS_HOOK_POINTER) {
        uintptr_t prev = *(volatile uintptr_t *)target_fn;
        *(volatile uintptr_t *)target_fn = hook_fn;
        *orig_fn = prev;
        h->orig_fn = prev;
        h->type = CANOPUS_HOOK_POINTER;
        return id;
    }

    /* 3. SRAM INLINE FUNCTION HOOKING */
    if (hook_type == CANOPUS_HOOK_INLINE) {
        uint8_t *code = (uint8_t *)target_even;
        uint8_t *orig_buf = &s_trampolines[id][0];
        const uint16_t *insns = (const uint16_t *)target_even;
        uint32_t copy_len = 0u;
        uint32_t orig_pos;
        uint32_t p;
        uint32_t min_patch_len = (target_even & 2u) ? 10u : 8u;
        uint32_t jmp_offset = (target_even & 2u) ? 2u : 0u;

        /* Decode Thumb instructions and ensure at least min_patch_len bytes are scanned */
        while (copy_len < min_patch_len) {
            uint16_t w = insns[copy_len / 2u];
            uint16_t w_next = insns[(copy_len / 2u) + 1u];
            if (is_func_exit_insn(w, w_next)) {
                h->used = 0;
                return CANOPUS_SVC_ERR_UNSUPPORTED; /* function too short */
            }
            if (is_pcrel_insn(w, w_next)) {
                h->used = 0;
                return CANOPUS_SVC_ERR_UNSUPPORTED; /* contains PC-relative instruction */
            }
            copy_len += thumb_insn_len(w);
        }

        if (copy_len > sizeof(h->orig_bytes)) {
            h->used = 0;
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }

        canopus_memcpy(h->orig_bytes, code, copy_len);
        h->orig_len = (uint8_t)copy_len;

        /* Trampoline executes original instructions then jumps back to target_even + copy_len */
        canopus_memcpy(orig_buf, code, copy_len);
        orig_pos = copy_len;
        if ((orig_pos & 2u) != 0u) {
            /* Align to 4 bytes using NOP (0xBF00) */
            orig_buf[orig_pos] = 0x00;
            orig_buf[orig_pos + 1] = 0xBF;
            orig_pos += 2u;
        }
        orig_buf[orig_pos + 0] = 0xDF;
        orig_buf[orig_pos + 1] = 0xF8;
        orig_buf[orig_pos + 2] = 0x00;
        orig_buf[orig_pos + 3] = 0xF0;
        uint32_t resume_target = (uint32_t)((target_even + copy_len) | 1u);
        canopus_memcpy(&orig_buf[orig_pos + 4], &resume_target, 4);
        sync_cache(orig_buf, orig_pos + 8u);
        *orig_fn = (uintptr_t)orig_buf | 1u;
        h->orig_fn = *orig_fn;

        /* Overwrite target: if unaligned, insert 2-byte NOP first so LDR.W PC is 4-byte aligned */
        if (jmp_offset > 0u) {
            code[0] = 0x00;
            code[1] = 0xBF;
        }
        code[jmp_offset + 0] = 0xDF;
        code[jmp_offset + 1] = 0xF8;
        code[jmp_offset + 2] = 0x00;
        code[jmp_offset + 3] = 0xF0;
        uint32_t hook_target = (uint32_t)(hook_fn | 1u);
        canopus_memcpy(&code[jmp_offset + 4], &hook_target, 4);

        /* Fill remaining overwritten bytes with NOP (0xBF00) */
        for (p = jmp_offset + 8u; p < copy_len; p += 2u) {
            code[p] = 0x00;
            code[p + 1] = 0xBF;
        }
        sync_cache(code, copy_len);
        h->type = CANOPUS_HOOK_INLINE;
        return id;
    }

    h->used = 0;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
#endif
}

int32_t canopus_hook_uninstall(uint32_t slot, int32_t hook_id)
{
    struct hook_entry *h;
    if (hook_id < 0 || hook_id >= (int32_t)CANOPUS_HOOK_MAX_SLOTS) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    h = &s_hooks[hook_id];
    if (!h->used || h->slot != slot) {
        return CANOPUS_SVC_ERR_NOTFOUND;
    }

    if (h->type == CANOPUS_HOOK_POINTER) {
        uintptr_t slot_addr = h->hook_slot_addr ? h->hook_slot_addr : h->target_fn;
        *(volatile uintptr_t *)slot_addr = h->orig_fn;
    }
#ifndef CANOPUS_HOST
    else if (h->type == CANOPUS_HOOK_FPB) {
        if (h->fpb_comp >= 0 && s_has_hw_fpb) {
            uint32_t k;
            for (k = 0u; k < 3u; k++) {
                uint32_t c = (uint32_t)h->fpb_comp + k;
                if (c < CANOPUS_FPB_MAX_COMPS) {
                    FPB_COMP(c) = 0u;
                    s_fpb_comp_used[c] = 0u;
                    s_fpb_remap_table[c] = 0u;
                }
            }
            sync_cache((void *)&FPB_COMP(h->fpb_comp), 12);
        }
        if (h->orig_len > 0) {
            uint8_t *code = (uint8_t *)(h->target_fn & ~1u);
            canopus_memcpy(code, h->orig_bytes, h->orig_len);
            sync_cache(code, h->orig_len);
        }
    } else if (h->type == CANOPUS_HOOK_INLINE && h->orig_len > 0) {
        uint8_t *code = (uint8_t *)(h->target_fn & ~1u);
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
}

int canopus_hook_is_active(int32_t hook_id)
{
    if (hook_id < 0 || hook_id >= (int32_t)CANOPUS_HOOK_MAX_SLOTS) {
        return 0;
    }
    return s_hooks[hook_id].used;
}

int canopus_hook_is_hooked(uintptr_t target_fn)
{
    uint32_t i;
    uintptr_t even = target_fn & ~1u;
    for (i = 0u; i < CANOPUS_HOOK_MAX_SLOTS; i++) {
        if (s_hooks[i].used) {
            if ((s_hooks[i].target_fn & ~1u) == even ||
                (s_hooks[i].hook_slot_addr && (s_hooks[i].hook_slot_addr & ~1u) == even)) {
                return 1;
            }
        }
    }
    return 0;
}
