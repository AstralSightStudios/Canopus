/*
 * canopus_lifecycle.c — lifecycle state machine (architecture §10.3).
 *
 * Transitions are table-driven. Every transition bumps the module
 * generation so retained callbacks/timers can detect staleness.
 */
#include "canopus_runtime.h"

struct transition {
    uint32_t from;
    uint32_t to;
    uint32_t classes; /* bitmask of allowed lifecycle classes */
};

#define CLASS_REMOVABLE (1u << CANOPUS_LIFECYCLE_REMOVABLE)
#define CLASS_RESIDENT (1u << CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION)
#define CLASS_ALWAYS (1u << CANOPUS_LIFECYCLE_ALWAYS_RESIDENT)
#define CLASS_PATCH (1u << CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED)
#define CLASS_ALL (CLASS_REMOVABLE | CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH)

static const struct transition transitions[] = {
    { CANOPUS_STATE_DISCOVERED, CANOPUS_STATE_VERIFIED, CLASS_ALL },
    { CANOPUS_STATE_VERIFIED, CANOPUS_STATE_INSTALLED, CLASS_ALL },
    { CANOPUS_STATE_INSTALLED, CANOPUS_STATE_DISABLED, CLASS_ALL },
    { CANOPUS_STATE_DISABLED, CANOPUS_STATE_ENABLED, CLASS_ALL },
    { CANOPUS_STATE_ENABLED, CANOPUS_STATE_DISABLED, CLASS_ALL },
    { CANOPUS_STATE_ENABLED, CANOPUS_STATE_LOADING, CLASS_ALL },
    { CANOPUS_STATE_LOADING, CANOPUS_STATE_PREPARING, CLASS_ALL },
    { CANOPUS_STATE_PREPARING, CANOPUS_STATE_READY, CLASS_ALL },
    { CANOPUS_STATE_READY, CANOPUS_STATE_ACTIVE, CLASS_ALL },

    /* removable unload path */
    { CANOPUS_STATE_ACTIVE, CANOPUS_STATE_STOPPING, CLASS_REMOVABLE },
    { CANOPUS_STATE_STOPPING, CANOPUS_STATE_DRAINING, CLASS_REMOVABLE },
    { CANOPUS_STATE_DRAINING, CANOPUS_STATE_UNLOADED, CLASS_REMOVABLE },
    /* removable: disable == stop/drain/unload too */
    { CANOPUS_STATE_READY, CANOPUS_STATE_STOPPING, CLASS_REMOVABLE },
    { CANOPUS_STATE_DISABLED, CANOPUS_STATE_UNLOADED, CLASS_REMOVABLE },

    /* resident barrier */
    { CANOPUS_STATE_ACTIVE, CANOPUS_STATE_BOOT_RESIDENT,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    { CANOPUS_STATE_BOOT_RESIDENT, CANOPUS_STATE_DISABLED_NEXT_BOOT,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },

    /* failure paths */
    { CANOPUS_STATE_LOADING, CANOPUS_STATE_FAILED, CLASS_ALL },
    { CANOPUS_STATE_PREPARING, CANOPUS_STATE_FAILED, CLASS_ALL },
    { CANOPUS_STATE_READY, CANOPUS_STATE_FAILED, CLASS_ALL },
    { CANOPUS_STATE_ACTIVE, CANOPUS_STATE_FAIL_STOP,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    { CANOPUS_STATE_BOOT_RESIDENT, CANOPUS_STATE_FAIL_STOP,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    { CANOPUS_STATE_FAIL_STOP, CANOPUS_STATE_QUARANTINED_NEXT_BOOT,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },

    /* resident update/remove */
    { CANOPUS_STATE_BOOT_RESIDENT, CANOPUS_STATE_UPDATE_STAGED,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    { CANOPUS_STATE_UPDATE_STAGED, CANOPUS_STATE_REBOOT_REQUIRED, CLASS_ALL },
    { CANOPUS_STATE_BOOT_RESIDENT, CANOPUS_STATE_REMOVE_PENDING,
      CLASS_RESIDENT | CLASS_ALWAYS | CLASS_PATCH },
    { CANOPUS_STATE_REMOVE_PENDING, CANOPUS_STATE_REBOOT_REQUIRED, CLASS_ALL },
};

#define TRANSITIONS_LEN (sizeof(transitions) / sizeof(transitions[0]))

static uint32_t class_mask(uint32_t lifecycle_class)
{
    if (lifecycle_class >= 4u) {
        return 0u;
    }
    return 1u << lifecycle_class;
}

int canopus_lifecycle_allow(uint32_t from, uint32_t to,
                            uint32_t lifecycle_class)
{
    uint32_t mask = class_mask(lifecycle_class);
    uint32_t i;
    for (i = 0; i < TRANSITIONS_LEN; i++) {
        if (transitions[i].from == from && transitions[i].to == to &&
            (transitions[i].classes & mask) != 0u) {
            return 0;
        }
    }
    return -1;
}

int canopus_lifecycle_init(struct canopus_lifecycle_v1 *lc,
                           uint32_t lifecycle_class)
{
    if (lc == 0 || lifecycle_class >= 4u) {
        return -1;
    }
    lc->state = CANOPUS_STATE_DISCOVERED;
    lc->lifecycle_class = lifecycle_class;
    lc->generation = 1u;
    lc->reserved = 0;
    return 0;
}

int canopus_lifecycle_transition(struct canopus_lifecycle_v1 *lc,
                                 uint32_t to_state)
{
    if (lc == 0) {
        return -1;
    }
    if (canopus_lifecycle_allow(lc->state, to_state, lc->lifecycle_class) != 0) {
        return -1;
    }
    lc->state = to_state;
    lc->generation += 1u;
    return 0;
}

const char *canopus_state_name(uint32_t state)
{
    switch (state) {
    case CANOPUS_STATE_DISCOVERED: return "discovered";
    case CANOPUS_STATE_VERIFIED: return "verified";
    case CANOPUS_STATE_INSTALLED: return "installed";
    case CANOPUS_STATE_DISABLED: return "disabled";
    case CANOPUS_STATE_ENABLED: return "enabled";
    case CANOPUS_STATE_LOADING: return "loading";
    case CANOPUS_STATE_PREPARING: return "preparing";
    case CANOPUS_STATE_READY: return "ready";
    case CANOPUS_STATE_ACTIVE: return "active";
    case CANOPUS_STATE_STOPPING: return "stopping";
    case CANOPUS_STATE_DRAINING: return "draining";
    case CANOPUS_STATE_UNLOADED: return "unloaded";
    case CANOPUS_STATE_BOOT_RESIDENT: return "boot-resident";
    case CANOPUS_STATE_DISABLED_NEXT_BOOT: return "disabled-next-boot";
    case CANOPUS_STATE_FAILED: return "failed";
    case CANOPUS_STATE_FAIL_STOP: return "fail-stop";
    case CANOPUS_STATE_QUARANTINED_NEXT_BOOT: return "quarantined-next-boot";
    case CANOPUS_STATE_UPDATE_STAGED: return "update-staged";
    case CANOPUS_STATE_REBOOT_REQUIRED: return "reboot-required";
    case CANOPUS_STATE_REMOVE_PENDING: return "remove-pending";
    default: return "<unknown>";
    }
}
