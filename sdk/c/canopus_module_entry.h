/*
 * canopus_module_entry.h — the one way a module hands its descriptor to the
 * Supervisor, on every family.
 *
 * The two loaders give a module's constructor different things:
 *
 *  - Stock modlib (Band 10 Pro): the constructor gets no context (r0 is not
 *    meaningful). The only channel is /dev/canopus: the module writes one CMR1
 *    frame naming its descriptor (canopus_module_registration.h), through the
 *    target's own NuttX entry points. The Supervisor then calls prepare/activate
 *    with the module's context.
 *  - Canopus' loader (Band 11, and portable modules): the init entry receives
 *    the context in r0 and registers through ctx->services->module_register.
 *
 * canopus_module_register / CANOPUS_MODULE_ENTRY pick the path at compile time,
 * so a module's source is the same on both: a target module built against a
 * stock-modlib target (generated config without CANOPUS_SUP_CUSTOM_LOADER)
 * registers with a CMR1 write, anything else through the service table. A
 * portable module has no firmware address and can only take the second path,
 * which is why it loads only where Canopus' loader does.
 *
 *     static const struct canopus_module_descriptor_v1 s_descriptor = { ... };
 *     CANOPUS_MODULE_ENTRY(s_descriptor)
 *
 * Include this header before the module's own includes that depend on the
 * target (it pulls in the exact target's generated config and veneer).
 */
#ifndef CANOPUS_MODULE_ENTRY_H
#define CANOPUS_MODULE_ENTRY_H

#include <stdint.h>

#include "canopus_abi.h"
#include "canopus_services.h"
#include "canopus_module_registration.h"

#if defined(__has_include)
#if __has_include("canopus_target_config.h")
#include "canopus_target_config.h"
#endif
#if __has_include("canopus_band11_addresses.h")
#include "canopus_band11_addresses.h"
#endif
#if __has_include("canopus_veneer.h")
#include "canopus_veneer.h"
#endif
#endif

/* A target module built for a stock-modlib family: the generated config names
 * the target and does not mark it as a custom loader. Portable modules include
 * no target config and always register through the service table. */
#if defined(CANOPUS_TARGET_ID) && !defined(CANOPUS_SUP_CUSTOM_LOADER)
#if !defined(CANOPUS_SUP_NUTTX_OPEN)
#error "stock-modlib target without the NuttX open entry point; CMR1 registration cannot be built"
#endif
#define CANOPUS_MODULE_STOCK_MODLIB 1
#endif

/* Registers `descriptor`. Returns 0 once the Supervisor took it. `ctx` is the
 * constructor's argument; the stock-modlib path ignores it. On a stock-modlib
 * target a firmware that is not the one this module was built for is refused
 * before anything else happens. */
static inline int canopus_module_register(
    const struct canopus_context_v1 *ctx,
    const struct canopus_module_descriptor_v1 *descriptor)
{
    if (descriptor == 0) return -1;
#if defined(CANOPUS_MODULE_STOCK_MODLIB)
    {
        typedef int (*open_fn)(const char *, int, ...);
        struct canopus_module_registration_io_v1 io;
        int close_error = 0;
        (void)ctx;
        if (canopus_identity_guard() != 0) return -1;
        io.open = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        io.write = canopus_fw_write;
        io.close = canopus_fw_close;
        io.write_flags = 2;
        return canopus_module_register_fd(&io, (uint32_t)(uintptr_t)descriptor,
                                          (const char *)descriptor->module_id,
                                          &close_error);
    }
#else
    if (!canopus_services_has(ctx, CANOPUS_SVC_MODULE, 1)) return -1;
    return ctx->services->module_register(ctx, descriptor);
#endif
}

/* The module's constructor: registers the descriptor object (an lvalue). */
#define CANOPUS_MODULE_ENTRY(descriptor_) \
    __attribute__((constructor)) static void canopus_module_entry( \
        const struct canopus_context_v1 *ctx) \
    { \
        (void)canopus_module_register(ctx, &(descriptor_)); \
    }

#endif /* CANOPUS_MODULE_ENTRY_H */
