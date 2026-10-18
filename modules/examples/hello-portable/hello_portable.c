/*
 * hello_portable.c — portable example module (ADR-CAN-006).
 *
 * Uses only the Supervisor service table: no firmware address, so the same
 * ELF loads on every firmware whose Supervisor implements the MODULE, LOG and
 * FS groups. It registers through the service-table path of
 * canopus_module_entry.h, so it loads only where Canopus' loader runs.
 */
#include "canopus_module_entry.h"

#define HP_MODULE_ID "hello_portable"

static const struct canopus_context_v1 *s_ctx;
static uint32_t s_activations;

static void hp_log(uint32_t level, const char *message)
{
    if (canopus_services_has(s_ctx, CANOPUS_SVC_LOG, 1)) {
        s_ctx->services->log_write(s_ctx, level, message);
    }
}

static int32_t hp_prepare(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;
    return 0;
}

static int32_t hp_activate(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;
    s_activations++;
    hp_log(CANOPUS_LOG_INFO, "hello_portable active");
    if (canopus_services_has(ctx, CANOPUS_SVC_FS, 1)) {
        int32_t h = ctx->services->fs_open(ctx, "activations", CANOPUS_FS_WRITE);
        if (h >= 0) {
            (void)ctx->services->fs_write(ctx, h, &s_activations,
                                          sizeof(s_activations));
            (void)ctx->services->fs_close(ctx, h);
        }
    }
    return 0;
}

static int32_t hp_deactivate(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

static int32_t hp_stop(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    s_ctx = 0;
    return 0;
}

static int32_t hp_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    0u,
    HP_MODULE_ID,
    "1.0.0",
    "portable-example",
    "portable-thumbv8m",
    hp_prepare,
    hp_activate,
    hp_deactivate,
    hp_stop,
    hp_query,
    0,
    0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
