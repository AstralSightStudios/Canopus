/* canopus_supervisor.c — device-side supervisor (Phase 5) implementation.
 *
 * Host-testable: the command/status ABI and module-slot lifecycle semantics
 * live here; the platform hooks (device registration, actual load/unload,
 * package staging) are behind canopus_supervisor_platform.h.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

static uint32_t sup_result_state(int rc)
{
    if (rc < 0) {
        return CANOPUS_RESULT_FAILED;
    }
    return CANOPUS_RESULT_COMPLETED;
}

static int module_lifecycle_ok(uint32_t lifecycle_class)
{
    return lifecycle_class <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED;
}

int canopus_supervisor_init(struct canopus_supervisor_v1 *sup,
                            uint32_t framework_revision,
                            const struct canopus_sup_platform_v1 *platform,
                            void *cookie)
{
    canopus_memset(sup, 0, sizeof(*sup));
    sup->abi = CANOPUS_SUP_ABI;
    sup->framework_revision = framework_revision;
    sup->platform = platform;
    sup->platform_cookie = cookie;
    sup->selected = -1;
    return 0;
}

int canopus_supervisor_validate_command(const uint8_t command[CANOPUS_SUP_COMMAND_SIZE])
{
    uint32_t magic;
    if (command == 0) {
        return -1;
    }
    magic = (uint32_t)command[0] | ((uint32_t)command[1] << 8) |
            ((uint32_t)command[2] << 16) | ((uint32_t)command[3] << 24);
    if (magic != CANOPUS_SUP_CMD_MAGIC) {
        return -1;
    }
    return 0;
}

static uint32_t cmd_word(const uint8_t command[CANOPUS_SUP_COMMAND_SIZE],
                         unsigned int idx)
{
    unsigned int o = 4 + idx * 4;
    return (uint32_t)command[o] | ((uint32_t)command[o + 1] << 8) |
           ((uint32_t)command[o + 2] << 16) | ((uint32_t)command[o + 3] << 24);
}

uint32_t canopus_supervisor_handle_command(struct canopus_supervisor_v1 *sup,
                                           const uint8_t command[CANOPUS_SUP_COMMAND_SIZE])
{
    uint32_t op, arg0, arg1;
    uint32_t rc = CANOPUS_RESULT_REJECTED;
    int slot;

    if (canopus_supervisor_validate_command(command) != 0) {
        sup->pending_op = 0;
        sup->pending_state = CANOPUS_RESULT_REJECTED;
        sup->error_code = -1;
        return sup->pending_state;
    }
    op = cmd_word(command, 0);
    arg0 = cmd_word(command, 1);
    arg1 = cmd_word(command, 2);

    sup->pending_op = op;
    sup->selected = (int32_t)arg0;

    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
        rc = CANOPUS_RESULT_COMPLETED;
        break;

    case CANOPUS_SUP_CMD_INSTALL: {
        /* The Lua side stages the package (path via the platform); INSTALL
         * just confirms the platform has it. */
        if (sup->platform && sup->platform->stage_package) {
            rc = sup->platform->stage_package(sup->platform_cookie, 0) == 0
                     ? CANOPUS_RESULT_COMPLETED
                     : CANOPUS_RESULT_FAILED;
        } else {
            rc = CANOPUS_RESULT_FAILED;
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
    case CANOPUS_SUP_CMD_UPDATE:
    case CANOPUS_SUP_CMD_ROLLBACK: {
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0) {
            rc = CANOPUS_RESULT_DISALLOWED;
            break;
        }
        if (op == CANOPUS_SUP_CMD_ENABLE ||
            op == CANOPUS_SUP_CMD_DISABLE ||
            op == CANOPUS_SUP_CMD_REMOVE) {
            /* Lifecycle-aware: a resident class has no unload path, so only
             * removable classes get real disable/remove. Next-boot semantics
             * are reported as REBOOT_REQUIRED for resident classes. */
            if (sup->modules[slot].lifecycle_class != CANOPUS_LIFECYCLE_REMOVABLE) {
                if (op == CANOPUS_SUP_CMD_REMOVE) {
                    sup->modules[slot].state = CANOPUS_STATE_REMOVE_PENDING;
                    rc = CANOPUS_RESULT_REBOOT_REQUIRED;
                } else {
                    sup->modules[slot].state = CANOPUS_STATE_DISABLED_NEXT_BOOT;
                    rc = CANOPUS_RESULT_REBOOT_REQUIRED;
                }
                break;
            }
        }
        if (op == CANOPUS_SUP_CMD_REMOVE) {
            if (sup->platform && sup->platform->unload_module) {
                rc = sup_result_state(sup->platform->unload_module(
                    sup->platform_cookie, (uint32_t)slot));
            } else {
                rc = CANOPUS_RESULT_FAILED;
            }
            if (rc == CANOPUS_RESULT_COMPLETED) {
                sup->modules[slot].state = CANOPUS_STATE_UNLOADED;
                sup->modules[slot].flags &= ~1u;
            }
        } else if (op == CANOPUS_SUP_CMD_DISABLE) {
            sup->modules[slot].state = CANOPUS_STATE_DISABLED;
            rc = CANOPUS_RESULT_COMPLETED;
        } else if (op == CANOPUS_SUP_CMD_ENABLE) {
            if (sup->platform && sup->platform->load_module) {
                int st = sup->platform->load_module(
                    sup->platform_cookie, (uint32_t)slot, 0,
                    sup->modules[slot].lifecycle_class);
                rc = sup_result_state(st);
                if (rc == CANOPUS_RESULT_COMPLETED) {
                    sup->modules[slot].state =
                        (uint32_t)(st < 0 ? CANOPUS_STATE_FAILED : st);
                }
            } else {
                rc = CANOPUS_RESULT_FAILED;
            }
        } else { /* UPDATE / ROLLBACK */
            sup->modules[slot].state = CANOPUS_STATE_UPDATE_STAGED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
        sup->safe_mode = 1u;
        rc = CANOPUS_RESULT_COMPLETED;
        break;

    default:
        rc = CANOPUS_RESULT_REJECTED;
        break;
    }

    sup->pending_state = rc;
    sup->error_code = 0;
    (void)arg1;
    return rc;
}

int canopus_supervisor_render_status(const struct canopus_supervisor_v1 *sup,
                                     uint8_t out[CANOPUS_SUP_STATUS_SIZE])
{
    uint32_t i;
    if (out == 0) {
        return -1;
    }
    canopus_memset(out, 0, CANOPUS_SUP_STATUS_SIZE);
#define PUT32(o, v) \
    do { \
        uint32_t _v = (uint32_t)(v); \
        out[o] = (uint8_t)(_v & 0xff); \
        out[(o) + 1] = (uint8_t)((_v >> 8) & 0xff); \
        out[(o) + 2] = (uint8_t)((_v >> 16) & 0xff); \
        out[(o) + 3] = (uint8_t)((_v >> 24) & 0xff); \
    } while (0)
    PUT32(0, CANOPUS_SUP_STATUS_MAGIC);
    PUT32(4, sup->abi);
    PUT32(8, sup->framework_revision);
    PUT32(12, sup->safe_mode);
    PUT32(16, sup->module_count);
    PUT32(20, sup->pending_op);
    PUT32(24, sup->pending_state);
    PUT32(28, sup->flags);
    PUT32(32, sup->error_code);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        uint32_t o = 128u + i * CANOPUS_SUP_MODULE_SLOT_STRIDE;
        const struct canopus_sup_module_v1 *m = &sup->modules[i];
        PUT32(o + 0, m->state);
        PUT32(o + 4, m->lifecycle_class);
        PUT32(o + 8, m->version);
        PUT32(o + 12, m->flags);
    }
#undef PUT32
    return 0;
}

/* Host convenience: record a newly installed module into a slot. */
int canopus_supervisor_add_module(struct canopus_supervisor_v1 *sup,
                                  uint32_t lifecycle_class,
                                  uint32_t version,
                                  uint32_t signature_ok)
{
    uint32_t i;
    if (!module_lifecycle_ok(lifecycle_class)) {
        sup->error_code = -2;
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state == 0) {
            sup->modules[i].state = CANOPUS_STATE_INSTALLED;
            sup->modules[i].lifecycle_class = lifecycle_class;
            sup->modules[i].version = version;
            sup->modules[i].flags = signature_ok ? 1u : 0u;
            sup->module_count++;
            return (int)i;
        }
    }
    sup->error_code = -3;
    return -1;
}
