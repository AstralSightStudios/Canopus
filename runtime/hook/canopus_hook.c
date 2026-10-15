/*
 * canopus_hook.c — Universal dynamic function hooking for Canopus on Cortex-M33.
 */
#include "canopus_hook.h"
#include "canopus_memory.h"

struct hook_entry {
    int used;
    uint32_t slot;
    uintptr_t target_fn;
    uintptr_t hook_fn;
    uintptr_t orig_fn;
    canopus_hook_type_t type;
    int fpb_comp;
    uint8_t orig_bytes[16];
    uint8_t orig_len;
    uint8_t trampoline[32];
};

static struct hook_entry s_hooks[CANOPUS_HOOK_MAX_SLOTS];

#ifndef CANOPUS_HOST

/* Hardware Cortex-M33 FPB registers */
#define FPB_CTRL        (*(volatile uint32_t *)0xE0002000u)
#define FPB_REMAP       (*(volatile uint32_t *)0xE0002004u)
#define FPB_COMP(n)     (*(volatile uint32_t *)(0xE0002008u + (uint32_t)(n) * 4u))

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

static inline uint32_t thumb_insn_len(uint16_t w)
{
    return ((w & 0xF800u) == 0xE800u || (w & 0xF000u) == 0xF000u) ? 4u : 2u;
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
#ifndef CANOPUS_HOST
    for (i = 0u; i < CANOPUS_FPB_MAX_COMPS; i++) {
        s_fpb_comp_used[i] = 0u;
        FPB_COMP(i) = 0u;
    }
    FPB_REMAP = (uint32_t)(uintptr_t)&s_fpb_remap_table[0];
    FPB_CTRL = FPB_KEY | FPB_ENABLE;
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    s_has_hw_fpb = ((FPB_CTRL & FPB_ENABLE) != 0u) ? 1 : 0;
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

    id = alloc_hook_slot();
    if (id < 0) return CANOPUS_SVC_ERR_NOMEM;

    h = &s_hooks[id];
    h->used = 1;
    h->slot = slot;
    h->target_fn = target_fn;
    h->hook_fn = hook_fn;
    h->type = hook_type;
    h->fpb_comp = -1;

    target_even = target_fn & ~1u;

#ifdef CANOPUS_HOST
    (void)target_even;
    /* Host testing simulation: safe pointer substitution or mock trampoline */
    if (hook_type == CANOPUS_HOOK_POINTER) {
        *orig_fn = *(uintptr_t *)target_fn;
        *(uintptr_t *)target_fn = hook_fn;
    } else {
        *orig_fn = target_fn;
    }
    h->orig_fn = *orig_fn;
    return id;
#else

    /* Auto-selection: Flash (0x0C000000..0x0E000000) requires FPB */
    if (hook_type == CANOPUS_HOOK_AUTO) {
        if (target_even >= 0x0C000000u && target_even < 0x0E000000u) {
            hook_type = CANOPUS_HOOK_FPB;
        } else {
            hook_type = CANOPUS_HOOK_INLINE;
        }
    }

    if (hook_type == CANOPUS_HOOK_POINTER) {
        uintptr_t prev = *(volatile uintptr_t *)target_fn;
        *(volatile uintptr_t *)target_fn = hook_fn;
        *orig_fn = prev;
        h->orig_fn = prev;
        return id;
    }

    if (hook_type == CANOPUS_HOOK_FPB) {
        uint8_t *orig_buf = &s_trampolines[id][0];
        const uint16_t *insns = (const uint16_t *)target_even;
        uint32_t copy_len = 0u;
        uint32_t orig_pos;
        int comp;

        /* Decode instructions at target to cover at least 12 bytes (3 words) */
        while (copy_len < 12u) {
            uint16_t w = insns[copy_len / 2u];
            uint32_t ilen = thumb_insn_len(w);
            copy_len += ilen;
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

        if (comp >= 0) {
            /* Configure FPB remap entries:
             * Word 0: MOVW R12, #(hook_fn & 0xFFFF)
             * Word 1: MOVT R12, #((hook_fn >> 16) & 0xFFFF)
             * Word 2: BX R12; NOP */
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
            /* In emulator environments where FPB MMIO is not implemented in QEMU:
             * Flash memory region alias in QEMU is writable, so we apply the 3-word jump directly */
            uint16_t *code16 = (uint16_t *)target_even;
            emit_thumb2_movw_r12(&code16[0], (uint16_t)(hook_fn & 0xFFFFu));
            emit_thumb2_movt_r12(&code16[2], (uint16_t)((hook_fn >> 16) & 0xFFFFu));
            emit_thumb2_bx_r12_nop(&code16[4]);
            for (uint32_t p = 6u; p < copy_len / 2u; p++) {
                code16[p] = 0xBF00u; /* NOP */
            }
            sync_cache(code16, copy_len);
        }
        return id;
    }

    if (hook_type == CANOPUS_HOOK_INLINE) {
        uint8_t *code = (uint8_t *)target_even;
        uint8_t *orig_buf = &s_trampolines[id][0];
        const uint16_t *insns = (const uint16_t *)target_even;
        uint32_t copy_len = 0u;
        uint32_t orig_pos;
        uint8_t imm;
        uint32_t p;

        /* Decode Thumb instructions so we never split a 32-bit instruction */
        while (copy_len < 8u) {
            uint16_t w = insns[copy_len / 2u];
            uint32_t ilen = ((w & 0xF800u) == 0xE800u || (w & 0xF000u) == 0xF000u) ? 4u : 2u;
            copy_len += ilen;
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
        *(uint32_t *)&orig_buf[orig_pos + 4] = (uint32_t)((target_even + copy_len) | 1u);
        sync_cache(orig_buf, orig_pos + 8u);
        *orig_fn = (uintptr_t)orig_buf | 1u;
        h->orig_fn = *orig_fn;

        /* Overwrite target with LDR.W PC, [PC, #imm] -> hook_fn */
        imm = (target_even & 2u) ? 0x02 : 0x00;
        code[0] = 0xDF;
        code[1] = 0xF8;
        code[2] = imm;
        code[3] = 0xF0;
        *(uint32_t *)&code[4] = (uint32_t)(hook_fn | 1u);
        /* Fill remaining overwritten bytes with NOP (0xBF00) */
        for (p = 8u; p < copy_len; p += 2u) {
            code[p] = 0x00;
            code[p + 1] = 0xBF;
        }
        sync_cache(code, copy_len);
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

#ifndef CANOPUS_HOST
    if (h->type == CANOPUS_HOOK_POINTER) {
        *(volatile uintptr_t *)h->target_fn = h->orig_fn;
    } else if (h->type == CANOPUS_HOOK_FPB) {
        if (h->fpb_comp >= 0) {
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
        if (!s_has_hw_fpb && h->orig_len > 0) {
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
        if (s_hooks[i].used && (s_hooks[i].target_fn & ~1u) == even) {
            return 1;
        }
    }
    return 0;
}
