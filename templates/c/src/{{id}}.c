/* {{id}} - Canopus module. */
#include "canopus_module_entry.h"

static int32_t on_prepare(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_activate(const struct canopus_context_v1 *ctx)
{
    if (canopus_services_has(ctx, CANOPUS_SVC_LOG, 1)) {
        ctx->services->log_write(ctx, CANOPUS_LOG_INFO, "{{id}} active");
    }
    return 0;
}

static int32_t on_deactivate(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_stop(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int32_t on_query(struct canopus_status_writer_v1 *writer) { (void)writer; return 0; }

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,
    "{{id}}", "0.1.0", "", {{descriptor_target}},
    on_prepare, on_activate, on_deactivate, on_stop, on_query, 0, 0,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
