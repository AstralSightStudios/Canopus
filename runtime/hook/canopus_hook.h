/*
 * canopus_hook.h — RAM pointer redirection and Thumb trampolines on Cortex-M33.
 *
 * Flash functions require developer-supplied Flash -> RAM dispatch mappings.
 * Unknown Flash targets return CANOPUS_SVC_ERR_UNSUPPORTED without patching code.
 * No firmware-specific mapping table, Flash writes or patch hardware is used.
 * CANOPUS_HOOK_FPB is a legacy request value: only a registered RAM mapping can
 * satisfy it. POINTER replaces a RAM slot; INLINE patches SRAM Thumb code only.
 * AUTO selects a registered mapping, an aligned pointer slot, or a Thumb inline
 * hook. Callers must distinguish code from data correctly (prefer explicit types).
 */
#ifndef CANOPUS_HOOK_H
#define CANOPUS_HOOK_H

#include <stdint.h>
#include <stddef.h>
#include "canopus_services.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_HOOK_MAX_SLOTS 16u

/* Initialize software state once at startup, before installing any hooks. */
void canopus_hook_engine_init(void);

/* Install a hook owned by module `slot`. Returns a hook id or a negative error.
 * Pointer originals are exact slot values, including NULL. An active physical
 * RAM slot cannot be hooked again through either a direct pointer or an alias.
 * Inline hooks copy only explicitly supported, PC-independent instructions;
 * control flow, PC operands, IT blocks and unknown opcodes return UNSUPPORTED.
 * Native host builds support real pointer hooks, not simulated inline hooks. */
int32_t canopus_hook_install(uint32_t slot,
                             uintptr_t target_fn,
                             uintptr_t hook_fn,
                             uintptr_t *orig_fn,
                             canopus_hook_type_t hook_type);

/* Restore and remove one hook owned by `slot`. */
int32_t canopus_hook_uninstall(uint32_t slot, int32_t hook_id);

/* Restore all hooks, then discard all mappings owned by `slot`. Must be called
 * before freeing the module's dispatch slots or executable memory. */
void canopus_hook_release_slot(uint32_t slot);

int canopus_hook_is_active(int32_t hook_id);

/* Includes registered aliases of an occupied physical RAM slot. */
int canopus_hook_is_hooked(uintptr_t target_fn);

/* Register a function -> pointer-aligned RAM dispatch slot mapping owned by
 * module `slot`. Only that owner can install using this mapping. Thumb-bit
 * aliases share one mapping; changing an active mapping returns ERR_STATE.
 * Registration does not write the dispatch slot. No firmware addresses are
 * supplied by the engine; the developer must identify the correct RAM slot. */
int32_t canopus_hook_register_slot_map(uint32_t slot, uintptr_t flash_fn,
                                     uintptr_t ram_slot);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_HOOK_H */
