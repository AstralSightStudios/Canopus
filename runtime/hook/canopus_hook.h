/*
 * canopus_hook.h — Universal dynamic function hooking for Canopus on Cortex-M33.
 *
 * Supports three hooking mechanisms:
 *   1. CANOPUS_HOOK_FPB    : Cortex-M33 Flash Patch and Breakpoint unit (0xE0002000).
 *                            Hardware-remaps Flash instruction fetches to an SRAM
 *                            trampoline without modifying Flash memory.
 *   2. CANOPUS_HOOK_INLINE : Thumb-2 8-byte LDR.W PC trampoline for SRAM functions.
 *   3. CANOPUS_HOOK_POINTER: Atomic replacement of function pointers in RAM tables.
 *
 * CANOPUS_HOOK_AUTO automatically selects the safest mechanism based on target address.
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
#define CANOPUS_FPB_MAX_COMPS  8u

/* Initialize the hook engine and FPB hardware (if present). */
void canopus_hook_engine_init(void);

/* Install a hook for module `slot`. Returns hook_id (0..CANOPUS_HOOK_MAX_SLOTS-1)
 * or negative error code. */
int32_t canopus_hook_install(uint32_t slot,
                             uintptr_t target_fn,
                             uintptr_t hook_fn,
                             uintptr_t *orig_fn,
                             canopus_hook_type_t hook_type);

/* Uninstall a specific hook by id. */
int32_t canopus_hook_uninstall(uint32_t slot, int32_t hook_id);

/* Release all active hooks owned by module `slot` (called on module unload). */
void canopus_hook_release_slot(uint32_t slot);

/* Query whether a hook is currently active. */
int canopus_hook_is_active(int32_t hook_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_HOOK_H */
