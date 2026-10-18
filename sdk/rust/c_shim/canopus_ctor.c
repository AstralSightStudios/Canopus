/*
 * canopus_ctor.c — constructor/destructor glue for Rust modules (CAN-RUST-004),
 * shared by `canopus build` and the examples' build scripts.
 *
 * The loaders run .init_array and retain .fini_array for rmmod. The Rust module
 * must not carry a personality/unwind implementation, so this C shim is the
 * module's constructor: it hands the crate's `canopus_module_descriptor` to the
 * Supervisor through canopus_module_entry.h, the same entry C modules use (a
 * CMR1 write on stock-modlib targets, ctx->services->module_register
 * elsewhere). The identity guard runs inside that entry on stock-modlib
 * targets, before anything else. Compile with -I sdk/c, the exact target's
 * generated directory and -DCANOPUS_TARGET=1.
 */
#include "canopus_module_entry.h"

extern const struct canopus_module_descriptor_v1 canopus_module_descriptor;
extern int canopus_mod_stop(const struct canopus_context_v1 *ctx);

CANOPUS_MODULE_ENTRY(canopus_module_descriptor)

__attribute__((destructor)) static void canopus_mod_dtor(void)
{
    (void)canopus_mod_stop(0);
}
