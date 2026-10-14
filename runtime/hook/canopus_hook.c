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

__attribute__((aligned(4)))
static uint8_t s_trampolines[CANOPUS_HOOK_MAX_SLOTS][24];

static inline void sync_cache(const void *addr, size_t len)
{
    (void)addr;
    (void)len;
    /* Cortex-M33 Memory barrier */
    __asm__ volatile("dsb\n\tisb" ::: "memory");
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
static int alloc_fpb_comp(void)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_FPB_MAX_COMPS; i++) {
        if (!s_fpb_comp_used[i]) {
            s_fpb_comp_used[i] = 1u;
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
        int comp = alloc_fpb_comp();
        uint16_t w0;
        uint32_t insn_len;
        uint8_t *orig_buf;

        if (comp < 0) {
            h->used = 0;
            return CANOPUS_SVC_ERR_STATE; /* no hardware FPB slots left */
        }
        h->fpb_comp = comp;

        /* Inspect the original instruction at target to construct orig_fn */
        w0 = *(const volatile uint16_t *)target_even;
        insn_len = ((w0 & 0xF800u) == 0xE800u || (w0 & 0xF000u) == 0xF000u) ? 4u : 2u;

        orig_buf = &s_trampolines[id][0];
        /* Copy original instruction */
        canopus_memcpy(orig_buf, (const void *)target_even, insn_len);
        if (insn_len == 2u) {
            /* Add NOP (0xBF00) so following LDR.W PC is aligned */
            orig_buf[2] = 0x00;
            orig_buf[3] = 0xBF;
            insn_len = 4u;
        }
        /* Append LDR.W PC, [PC, #0] (0xF8DF, 0xF000) */
        orig_buf[insn_len + 0u] = 0xDF;
        orig_buf[insn_len + 1u] = 0xF8;
        orig_buf[insn_len + 2u] = 0x00;
        orig_buf[insn_len + 3u] = 0xF0;
        /* Destination: target_even + original instruction length | 1 (Thumb) */
        *(uint32_t *)&orig_buf[insn_len + 4u] = (uint32_t)((target_even + (insn_len == 4u && w0 != 0xBF00 ? 4u : 2u)) | 1u);

        sync_cache(orig_buf, insn_len + 8u);
        *orig_fn = (uintptr_t)orig_buf | 1u;
        h->orig_fn = *orig_fn;

        /* Configure FPB remap entry: LDR.W PC, [PC, #0] to hook_fn */
        s_fpb_remap_table[comp] = 0xF000F8DFu; /* placeholder opcode or branch */

        /* Enable comparator: address bits [28:1] and bit 0=ENABLE */
        FPB_COMP(comp) = ((uint32_t)target_even & 0x1FFFFFFEu) | 1u;
        sync_cache((void *)&FPB_COMP(comp), 4);
        return id;
    }

    if (hook_type == CANOPUS_HOOK_INLINE) {
        uint8_t *code = (uint8_t *)target_even;
        uint8_t *orig_buf = &s_trampolines[id][0];

        canopus_memcpy(h->orig_bytes, code, 8);
        h->orig_len = 8;

        /* Trampoline executes original 8 bytes then jumps back to target_even + 8 */
        canopus_memcpy(orig_buf, code, 8);
        orig_buf[8] = 0xDF;
        orig_buf[9] = 0xF8;
        orig_buf[10] = 0x00;
        orig_buf[11] = 0xF0;
        *(uint32_t *)&orig_buf[12] = (uint32_t)((target_even + 8u) | 1u);
        sync_cache(orig_buf, 16);
        *orig_fn = (uintptr_t)orig_buf | 1u;
        h->orig_fn = *orig_fn;

        /* Overwrite target with LDR.W PC, [PC, #0] -> hook_fn */
        code[0] = 0xDF;
        code[1] = 0xF8;
        code[2] = 0x00;
        code[3] = 0xF0;
        *(uint32_t *)&code[4] = (uint32_t)(hook_fn | 1u);
        sync_cache(code, 8);
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
    } else if (h->type == CANOPUS_HOOK_FPB && h->fpb_comp >= 0) {
        FPB_COMP(h->fpb_comp) = 0u;
        s_fpb_comp_used[h->fpb_comp] = 0u;
        sync_cache((void *)&FPB_COMP(h->fpb_comp), 4);
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
