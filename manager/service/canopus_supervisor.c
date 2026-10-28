/* canopus_supervisor.c — device-side supervisor (Phase 5) implementation.
 *
 * Host-testable: the command/status ABI and module-slot lifecycle semantics
 * live here; the platform hooks (device registration, actual load/unload,
 * package staging) are behind canopus_supervisor_platform.h.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_supervisor_boot.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"

__attribute__((weak)) void canopus_manager_target_render_diagnostics(
    uint8_t out[36])
{
    canopus_memset(out, 0, 36u);
}

__attribute__((weak)) void canopus_manager_native_refresh(void) {}

static int module_lifecycle_ok(uint32_t lifecycle_class)
{
    return lifecycle_class <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED;
}

/* CAN-P0-005 revision (next-boot): a module is *loaded* only when its code
 * is actually resident this session. INSTALLED/DISABLED/ENABLED are all
 * non-resident bookkeeping states. */
static int sup_module_loaded(uint32_t state)
{
    return state == CANOPUS_STATE_ACTIVE || state == CANOPUS_STATE_READY ||
           state == CANOPUS_STATE_BOOT_RESIDENT;
}

/* CAN-P1-007: reclaim a slot (used after a remove intent is applied at
 * boot restore). */
static void sup_clear_slot(struct canopus_supervisor_v1 *sup, int slot)
{
    if (sup->modules[slot].state != 0) {
        sup->module_count--;
    }
    canopus_memset(&sup->modules[slot], 0, sizeof(sup->modules[slot]));
}

static int sup_find_slot_by_id(const struct canopus_supervisor_v1 *sup,
                               const char *token);

/* Public flags mirror the on-disk generation state. */
static void sup_refresh_flags(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    m->flags = 0u;
    if (m->state == 0u) return;
    if (pf != 0 && pf->gen_exists != 0 &&
        pf->gen_exists(sup->platform_cookie, (const char *)m->module_id,
                       (uint32_t)(m->active_gen ^ 1u)) > 0) {
        m->flags |= CANOPUS_SUP_FLAG_HAS_PREV;
    }
    if (m->pending_switch) m->flags |= CANOPUS_SUP_FLAG_UPDATE_PENDING;
}

static int sup_try_load_slot(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    int st;
    st = sup->platform->load_module(sup->platform_cookie, slot,
                                    (const char *)m->module_id, m->lifecycle_class);
    if (st < 0) {
        canopus_supervisor_release_services(sup, slot);
        m->descriptor = 0;
    }
    return st;
}

/* Loads a slot; a freshly switched generation that fails to load falls back
 * to the previous generation once. */
static int sup_load_slot(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint32_t version;
    uint32_t lifecycle;
    uint32_t switched = m->just_switched;
    int st = sup_try_load_slot(sup, slot);
    m->just_switched = 0u;
    if (st >= 0 || !switched || pf->gen_verify == 0) return st;
    if (pf->gen_verify(sup->platform_cookie, (const char *)m->module_id,
                       (uint32_t)(m->active_gen ^ 1u), &version, &lifecycle) != 0) {
        return st;
    }
    m->active_gen ^= 1u;
    m->pending_switch = 0u;
    m->version = version;
    m->lifecycle_class = lifecycle;
    st = sup_try_load_slot(sup, slot);
    /* Set after the retry: loading clears the platform's last diagnostic. */
    if (st >= 0) {
        m->activation_error = (uint32_t)CANOPUS_SUP_ERR_UPDATE_FALLBACK;
        sup->error_code = CANOPUS_SUP_ERR_UPDATE_FALLBACK;
    }
    return st;
}

/* Boot-time application of a pending generation switch (update or rollback).
 * Runs before modules are loaded; returns 1 when the registry changed. */
static int sup_apply_pending_switches(struct canopus_supervisor_v1 *sup)
{
    uint32_t i;
    int changed = 0;
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *m = &sup->modules[i];
        uint32_t version;
        uint32_t lifecycle;
        if (m->state == 0u || !m->pending_switch) continue;
        m->pending_switch = 0u;
        changed = 1;
        if (pf->gen_verify != 0 &&
            pf->gen_verify(sup->platform_cookie, (const char *)m->module_id,
                           (uint32_t)(m->active_gen ^ 1u), &version,
                           &lifecycle) == 0 &&
            lifecycle <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED) {
            m->active_gen ^= 1u;
            m->version = version;
            m->lifecycle_class = lifecycle;
            m->activation_error = 0u;
            m->just_switched = 1u;
        } else {
            sup->error_code = CANOPUS_SUP_ERR_UPDATE_ABANDONED;
        }
        sup_refresh_flags(sup, i);
        canopus_supervisor_module_changed(sup, i);
    }
    return changed;
}

static int sup_fixed_string_equal(const uint8_t *fixed, uint32_t capacity,
                                  const char *text)
{
    uint32_t i;
    if (fixed == 0 || text == 0) return 0;
    for (i = 0u; i < capacity; i++) {
        uint8_t value = (uint8_t)text[i];
        if (fixed[i] != value) return 0;
        if (value == 0u) return 1;
    }
    return 0;
}

static uint32_t sup_registry_error_encode(uint32_t stored_error)
{
    int32_t error = (int32_t)stored_error;
    uint64_t magnitude;
    uint64_t encoded;
    if (error < 0) {
        magnitude = (uint64_t)(-(int64_t)error);
        encoded = (magnitude << 1) - 1u;
    } else {
        encoded = (uint64_t)(uint32_t)error << 1;
    }
    if (encoded > 0x7fffffffu) {
        encoded = ((uint32_t)(-CANOPUS_SUP_ERR_ACTIVATE) << 1) - 1u;
    }
    return (uint32_t)encoded;
}

static uint32_t sup_registry_error_decode(uint32_t registry_flags)
{
    uint32_t encoded = registry_flags >> CANOPUS_SUP_REGISTRY_ERROR_SHIFT;
    uint32_t magnitude = (encoded >> 1) + (encoded & 1u);
    int32_t error = (encoded & 1u) != 0u
                        ? -(int32_t)magnitude : (int32_t)magnitude;
    return (uint32_t)error;
}

static uint32_t sup_activate_module(struct canopus_supervisor_v1 *sup,
                                    struct canopus_sup_module_v1 *module)
{
    int activate_rc;
    if (module->state != CANOPUS_STATE_READY ||
        module->intent != CANOPUS_SUP_INTENT_ENABLED) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
        return CANOPUS_RESULT_DISALLOWED;
    }
    if (module->descriptor == 0 || module->descriptor->activate == 0) {
        canopus_supervisor_release_services(sup, (uint32_t)(module - sup->modules));
        module->state = CANOPUS_STATE_FAILED;
        module->activation_error =
            (uint32_t)CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
        sup->error_code = CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
        return CANOPUS_RESULT_FAILED;
    }
    activate_rc = module->descriptor->activate(
        canopus_supervisor_context(sup, (uint32_t)(module - sup->modules)));
    if (activate_rc != 0) {
        canopus_supervisor_release_services(sup, (uint32_t)(module - sup->modules));
        module->state = CANOPUS_STATE_FAILED;
        module->activation_error = (uint32_t)activate_rc;
        sup->error_code = CANOPUS_SUP_ERR_ACTIVATE;
        return CANOPUS_RESULT_FAILED;
    }
    module->state =
        module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE
            ? CANOPUS_STATE_ACTIVE : CANOPUS_STATE_BOOT_RESIDENT;
    if ((int32_t)module->activation_error != CANOPUS_SUP_ERR_UPDATE_FALLBACK) {
        module->activation_error = 0u;
    }
    return CANOPUS_RESULT_COMPLETED;
}

/* ---- global boot marker (registry header word, CANOPUS_SUP_BOOT_*) ---- */

static uint32_t sup_boot_word(const struct canopus_supervisor_v1 *sup)
{
    uint32_t crashes = sup->crash_counter > 0xffu ? 0xffu : sup->crash_counter;
    uint32_t unfinished = sup->boot_unfinished > 0xffu ? 0xffu : sup->boot_unfinished;
    uint32_t word = unfinished | (crashes << CANOPUS_SUP_BOOT_CRASH_SHIFT);
    if (sup->safe_mode && !sup->exit_pending) {
        word |= CANOPUS_SUP_BOOT_SAFE |
                ((sup->safe_mode_reason << CANOPUS_SUP_BOOT_REASON_SHIFT) &
                 CANOPUS_SUP_BOOT_REASON_MASK);
    }
    if (sup->boot_trial || sup->exit_pending) word |= CANOPUS_SUP_BOOT_TRIAL;
    if (sup->boot_state == CANOPUS_BOOT_BOOTING) word |= CANOPUS_SUP_BOOT_BOOTING;
    return word;
}

static void sup_enter_safe_mode(struct canopus_supervisor_v1 *sup, uint32_t reason)
{
    sup->safe_mode = 1u;
    sup->safe_mode_reason = reason;
}

/* Reads the marker of the previous boot and decides whether this start is a
 * safe-mode start. The verdict is a pure function of the persisted marker, so it
 * needs no write of its own: the next start reaches the same answer until
 * EXIT_SAFE_MODE rewrites the marker. (The constructor also has little stack to
 * spare for a registry buffer.) */
static void sup_boot_check(struct canopus_supervisor_v1 *sup, uint32_t word)
{
    uint32_t unfinished = word & CANOPUS_SUP_BOOT_UNFINISHED_MASK;
    uint32_t reason = (word & CANOPUS_SUP_BOOT_REASON_MASK) >> CANOPUS_SUP_BOOT_REASON_SHIFT;
    int booting = (word & CANOPUS_SUP_BOOT_BOOTING) != 0u;
    sup->boot_saved = word;
    sup->crash_counter = word >> CANOPUS_SUP_BOOT_CRASH_SHIFT;
    sup->boot_trial = (word & CANOPUS_SUP_BOOT_TRIAL) != 0u;
    sup->boot_state = booting ? CANOPUS_BOOT_BOOTING : CANOPUS_BOOT_OK;
    if ((word & CANOPUS_SUP_BOOT_SAFE) != 0u) {
        sup->boot_unfinished = unfinished;
        sup_enter_safe_mode(sup, reason != 0u ? reason : CANOPUS_SAFE_MODE_USER_REQUESTED);
        return;
    }
    if (booting) unfinished++;
    sup->boot_unfinished = unfinished;
    if (booting && (sup->boot_trial || unfinished >= CANOPUS_SUP_UNFINISHED_LIMIT)) {
        sup_enter_safe_mode(sup, CANOPUS_SAFE_MODE_UNFINISHED_BOOT);
    } else if (sup->crash_counter >= CANOPUS_SUP_CRASH_THRESHOLD) {
        sup_enter_safe_mode(sup, CANOPUS_SAFE_MODE_CRASH);
    }
}

static void sup_boot_commit_ok(struct canopus_supervisor_v1 *sup)
{
    sup->boot_state = CANOPUS_BOOT_OK;
    sup->boot_unfinished = 0u;
    sup->boot_trial = 0u;
    /* A quarantine during this very boot still counts toward the next one. */
    if (sup->failing_module_id == 0u) sup->crash_counter = 0u;
}

static void sup_mark_stable(struct canopus_supervisor_v1 *sup,
                            struct canopus_sup_module_v1 *m)
{
    m->starts = 0u;
    if (m->watching) {
        m->watching = 0u;
        sup->unstable_modules--;
    }
}

/* The module's last CANOPUS_SUP_START_LIMIT boot starts all ended before it
 * proved stable. Falls back to the older generation when there is one: a fault
 * that began with an update is the update's. Otherwise the module is disabled. */
static void sup_handle_unstable(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint32_t version;
    uint32_t lifecycle;
    m->starts = 0u;
    canopus_supervisor_record_crash(sup);
    sup->failing_module_id = slot + 1u;
    if (pf->gen_verify != 0 &&
        pf->gen_verify(sup->platform_cookie, (const char *)m->module_id,
                       (uint32_t)(m->active_gen ^ 1u), &version, &lifecycle) == 0 &&
        version < m->version && lifecycle <= CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED) {
        m->active_gen ^= 1u;
        m->version = version;
        m->lifecycle_class = lifecycle;
        m->activation_error = (uint32_t)CANOPUS_SUP_ERR_UPDATE_FALLBACK;
        sup_refresh_flags(sup, slot);
        sup->error_code = CANOPUS_SUP_ERR_UPDATE_FALLBACK;
        return;
    }
    m->intent = CANOPUS_SUP_INTENT_DISABLED;
    m->activation_error = (uint32_t)CANOPUS_SUP_ERR_UNSTABLE;
    sup->error_code = CANOPUS_SUP_ERR_UNSTABLE;
}

/* Loads and activates one enabled, installed module at boot. The start is
 * written to the registry before the module's code runs (see
 * CANOPUS_SUP_START_LIMIT). Returns 1 when the module is up, 0 when it was
 * quarantined instead of started, and a negative value when it failed. */
static int sup_start_module(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *m = &sup->modules[slot];
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    int st;
    if (m->starts >= CANOPUS_SUP_START_LIMIT) {
        sup_handle_unstable(sup, slot);
        if (m->intent != CANOPUS_SUP_INTENT_ENABLED) return 0;
    }
    m->starts++;
    if (canopus_supervisor_save_registry(sup) != 0) {
        m->starts--;
        m->state = CANOPUS_STATE_FAILED;
        m->activation_error = (uint32_t)CANOPUS_SUP_ERR_REGISTRY;
        sup->error_code = CANOPUS_SUP_ERR_REGISTRY;
        return -1;
    }
    st = sup_load_slot(sup, slot);
    if (st < 0) {
        m->starts = 0u;
        m->state = CANOPUS_STATE_FAILED;
        m->activation_error = (uint32_t)st;
        if (sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_LOAD;
        }
        return -1;
    }
    m->state = (uint32_t)st;
    if (st == CANOPUS_STATE_READY &&
        sup_activate_module(sup, m) != CANOPUS_RESULT_COMPLETED) {
        m->starts = 0u;
        return -1;
    }
    if (pf->monotonic_ms == 0) {
        m->starts = 0u;
    } else {
        m->watching = 1u;
        m->started_ms = pf->monotonic_ms(sup->platform_cookie);
        sup->unstable_modules++;
    }
    return 1;
}

void canopus_supervisor_stability_poll(struct canopus_supervisor_v1 *sup,
                                       uint32_t now_ms)
{
    uint32_t i;
    int cleared = 0;
    if (sup->unstable_modules == 0u) return;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *m = &sup->modules[i];
        if (m->watching && (uint32_t)(now_ms - m->started_ms) >= CANOPUS_SUP_STABLE_MS) {
            sup_mark_stable(sup, m);
            cleared = 1;
        }
    }
    if (cleared && sup->unstable_modules == 0u && sup->boot_state == CANOPUS_BOOT_BOOTING) {
        sup_boot_commit_ok(sup);
    }
    if (cleared && canopus_supervisor_save_registry(sup) != 0) {
        sup->error_code = CANOPUS_SUP_ERR_REGISTRY;
    }
}

void canopus_supervisor_boot_notify(struct canopus_supervisor_v1 *sup)
{
    static char body[192];
    struct canopus_text_writer_v1 text;
    const struct canopus_sup_module_v1 *recovered = 0;
    const char *title = "Canopus";
    uint32_t i;
    uint32_t count = 0u;
    if (!sup->boot_notice_ready || !sup->boot_restored || sup->boot_notice_sent) return;
    sup->boot_notice_sent = 1u;
    if (sup->platform->notify == 0) return;
    canopus_text_writer_init(&text, body, sizeof(body));
    if (sup->safe_mode) {
        const char *reason;
        title = "Canopus 安全模式";
        switch (sup->safe_mode_reason) {
        case CANOPUS_SAFE_MODE_USER_REQUESTED: reason = "已手动进入安全模式。"; break;
        case CANOPUS_SAFE_MODE_CRASH: reason = "模块连续启动失败。"; break;
        case CANOPUS_SAFE_MODE_UNFINISHED_BOOT: reason = "上次启动未完成。"; break;
        case CANOPUS_SAFE_MODE_STORE_CORRUPT: reason = "模块注册表损坏。"; break;
        default: reason = "框架协议不匹配。"; break;
        }
        canopus_text_writer_append(&text, reason);
        canopus_text_writer_append(&text, "模块仍已安装，本次暂不加载，请打开管理器恢复。");
    } else {
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            const struct canopus_sup_module_v1 *module = &sup->modules[i];
            if (module->state != 0u &&
                ((int32_t)module->activation_error == CANOPUS_SUP_ERR_UNSTABLE ||
                 (int32_t)module->activation_error == CANOPUS_SUP_ERR_UPDATE_FALLBACK)) {
                if (recovered == 0) recovered = module;
                count++;
            }
        }
        if (recovered != 0) {
            title = "Canopus 模块恢复";
            canopus_text_writer_append(&text, (const char *)recovered->module_id);
            canopus_text_writer_append(&text, count > 1u ? " 等模块已自动停用或回退，" :
                (int32_t)recovered->activation_error == CANOPUS_SUP_ERR_UNSTABLE
                    ? " 因连续启动失败已停用，" : " 已自动回退到旧版本，");
            canopus_text_writer_append(&text, "仍已安装，请打开管理器查看。");
        } else if (sup->error_code != CANOPUS_SUP_ERR_NONE) {
            title = "Canopus 启动异常";
            canopus_text_writer_append(&text, "模块恢复未完成，请打开管理器查看原因。");
        } else {
            canopus_text_writer_append(&text, "Canopus 已加载，管理器可用。");
        }
    }
    (void)sup->platform->notify(sup->platform_cookie, "Canopus", title, body, 0);
}

void canopus_supervisor_boot_ready(struct canopus_supervisor_v1 *sup)
{
    sup->boot_notice_ready = 1u;
    canopus_supervisor_boot_notify(sup);
}

int canopus_supervisor_register_descriptor(
    struct canopus_supervisor_v1 *sup, const char *module_id,
    const struct canopus_module_descriptor_v1 *descriptor)
{
    uint32_t i;
    if (sup == 0 || module_id == 0 || descriptor == 0 ||
        canopus_supervisor_bus_dispatching() || sup->loading_slot < 0 ||
        (uint32_t)sup->loading_slot >= CANOPUS_SUP_MODULE_SLOTS ||
        canopus_module_descriptor_check(descriptor) != 0 ||
        sup->platform == 0 || sup->platform->target_id == 0 ||
        !(sup_fixed_string_equal(descriptor->target_id,
                                 sizeof(descriptor->target_id),
                                 sup->platform->target_id) ||
          sup_fixed_string_equal(descriptor->target_id,
                                 sizeof(descriptor->target_id),
                                 CANOPUS_PORTABLE_TARGET_ID))) {
        return -1;
    }
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        if ((int32_t)i == sup->loading_slot && module->state != 0u &&
            sup_fixed_string_equal(module->module_id,
                                   sizeof(module->module_id), module_id) &&
            sup_fixed_string_equal(descriptor->module_id,
                                   sizeof(descriptor->module_id), module_id)) {
            module->descriptor = descriptor;
            return 0;
        }
    }
    return -1;
}

int canopus_supervisor_init(struct canopus_supervisor_v1 *sup,
                            uint32_t framework_revision,
                            const struct canopus_sup_platform_v1 *platform,
                            void *cookie)
{
    if (sup == 0) {
        return -1; /* CAN-P2-001: never dereference a NULL supervisor */
    }
    canopus_memset(sup, 0, sizeof(*sup));
    sup->abi = CANOPUS_SUP_ABI;
    sup->framework_revision = framework_revision;
    sup->platform = platform;
    sup->platform_cookie = cookie;
    sup->selected = -1;
    sup->loading_slot = -1;
    sup->last_kind = 0; /* no CPC2 response pending */
    canopus_pending_init(&sup->pending);
    canopus_supervisor_services_bind(sup);
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

/* Core opcode dispatch shared by the legacy CPC1 path and the v2 path
 * (CAN-P0-008). `stage_arg` is the optional package path for INSTALL (v2
 * passes the request payload; the legacy path passes 0). */
/* CAN-P0-006: safe-mode command policy. Only read/diagnostic and next-boot
 * operations are allowed: QUERY, ROLLBACK, ENTER_SAFE_MODE, DISABLE and
 * REMOVE (which are next-boot only for every lifecycle class, so they never
 * run third-party code in safe mode). INSTALL, ENABLE and UPDATE — anything
 * that activates code — are rejected. */
static int sup_safe_mode_allows(const struct canopus_supervisor_v1 *sup,
                                uint32_t op, uint32_t arg0, const char *stage_arg)
{
    (void)sup;
    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
    case CANOPUS_SUP_CMD_ROLLBACK:
    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
    case CANOPUS_SUP_CMD_EXIT_SAFE_MODE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
        return 1;
    case CANOPUS_SUP_CMD_INSTALL:
        /* The payload-free stages publish the Manager and the launcher entry:
         * first-party code, and the way back out of safe mode. A package is
         * never staged here. */
        return stage_arg == 0 && arg0 <= 2u;
    case CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT:
        return 1;
    case CANOPUS_SUP_CMD_OPEN_MANAGER:
        return 1;
    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_UPDATE:
    case CANOPUS_SUP_CMD_SCAN:
    case CANOPUS_SUP_CMD_ACTIVATE:
        return 0; /* activation is never allowed in safe mode */
    default:
        return 0;
    }
}

static uint32_t sup_plan_rollback(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    struct canopus_sup_module_v1 *module = &sup->modules[slot];
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint32_t version;
    uint32_t lifecycle;
    int exists;
    if (module->pending_switch) {
        module->pending_switch = 0u;
        return CANOPUS_RESULT_COMPLETED;
    }
    if (pf == 0 || pf->gen_exists == 0 || pf->gen_verify == 0) {
        sup->error_code = CANOPUS_SUP_ERR_NO_PREV;
        return CANOPUS_RESULT_DISALLOWED;
    }
    exists = pf->gen_exists(sup->platform_cookie, (const char *)module->module_id,
                            (uint32_t)(module->active_gen ^ 1u));
    if (exists <= 0) {
        sup->error_code = CANOPUS_SUP_ERR_NO_PREV;
        return exists == 0 ? CANOPUS_RESULT_DISALLOWED : CANOPUS_RESULT_FAILED;
    }
    if (pf->gen_verify(sup->platform_cookie, (const char *)module->module_id,
                       (uint32_t)(module->active_gen ^ 1u), &version, &lifecycle) != 0) {
        sup->error_code = CANOPUS_SUP_ERR_NO_PREV;
        return CANOPUS_RESULT_FAILED;
    }
    module->pending_switch = 1u;
    return CANOPUS_RESULT_REBOOT_REQUIRED;
}

static uint32_t sup_dispatch(struct canopus_supervisor_v1 *sup, uint32_t op,
                             uint32_t arg0, uint32_t arg1,
                             const char *stage_arg)
{
    uint32_t rc = CANOPUS_RESULT_REJECTED;
    int slot;

    (void)arg1;

    if (sup->safe_mode && !sup_safe_mode_allows(sup, op, arg0, stage_arg)) {
        sup->error_code = CANOPUS_SUP_ERR_SAFE_MODE;
        return CANOPUS_RESULT_DISALLOWED;
    }
    switch (op) {
    case CANOPUS_SUP_CMD_QUERY:
        rc = CANOPUS_RESULT_COMPLETED;
        break;

    case CANOPUS_SUP_CMD_INSTALL: {
        uint32_t previous_occupied = 0u;
        uint32_t previous_count = sup->module_count;
        uint32_t i;
        int update_slot = stage_arg != 0 ? sup_find_slot_by_id(sup, stage_arg) : -1;
        struct canopus_sup_module_v1 previous_update;
        int staged = -1;
        if (update_slot >= 0) {
            canopus_memcpy(&previous_update, &sup->modules[update_slot],
                           sizeof(previous_update));
        }
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            if (sup->modules[i].state != 0u) previous_occupied |= 1u << i;
        }
        if (stage_arg == 0 && arg0 > 2u) {
            rc = CANOPUS_RESULT_FAILED;
        } else if (sup->platform && sup->platform->stage_package) {
            staged = sup->platform->stage_package(sup->platform_cookie,
                                                  stage_arg, arg0);
            /* A platform may already have recorded a specific code (for
             * example a duplicate/downgrade rejection). -1 is the generic
             * failure and falls through to CANOPUS_SUP_ERR_STAGE below. */
            if (staged < -1 && sup->error_code == CANOPUS_SUP_ERR_NONE) {
                sup->error_code = (uint32_t)staged;
            }
            rc = staged < 0 ? CANOPUS_RESULT_FAILED
                 : staged == 1 ? CANOPUS_RESULT_REBOOT_REQUIRED
                               : CANOPUS_RESULT_COMPLETED;
        } else {
            rc = CANOPUS_RESULT_FAILED;
        }
        /* §16.4: the registry is written on every slot change, INSTALL
         * included. A freshly installed (disabled) module must survive a
         * reboot even before it is ever enabled; otherwise "install then
         * reboot" silently loses it. Persist immediately after staging. */
        if (rc == CANOPUS_RESULT_COMPLETED ||
            rc == CANOPUS_RESULT_REBOOT_REQUIRED) {
            int persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                if (staged == 1 && update_slot >= 0) {
                    canopus_memcpy(&sup->modules[update_slot], &previous_update,
                                   sizeof(previous_update));
                }
                for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
                    if ((previous_occupied & (1u << i)) == 0u &&
                        sup->modules[i].state != 0u) {
                        canopus_memset(&sup->modules[i], 0,
                                       sizeof(sup->modules[i]));
                    }
                }
                sup->module_count = previous_count;
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        if (rc == CANOPUS_RESULT_FAILED &&
            sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE;
        }
        if (rc == CANOPUS_RESULT_COMPLETED || rc == CANOPUS_RESULT_REBOOT_REQUIRED) {
            for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
                if (((previous_occupied & (1u << i)) == 0u && sup->modules[i].state != 0u) ||
                    (staged == 1 && (int32_t)i == update_slot)) {
                    canopus_supervisor_module_changed(sup, i);
                }
            }
        }
        break;
    }

    case CANOPUS_SUP_CMD_ENABLE:
    case CANOPUS_SUP_CMD_DISABLE:
    case CANOPUS_SUP_CMD_REMOVE:
    case CANOPUS_SUP_CMD_ROLLBACK: {
        struct canopus_sup_module_v1 previous;
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0) {
            rc = CANOPUS_RESULT_DISALLOWED;
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            break;
        }
        /* CAN-P0-005 revision (next-boot): no lifecycle class hot-loads or
         * hot-unloads anymore. Every op records a boot intent and reports
         * REBOOT_REQUIRED; the next supervisor load applies the intent.
         * A remove-pending slot is committed and not re-targetable. */
        if (sup->modules[slot].state == CANOPUS_STATE_REMOVE_PENDING &&
            op != CANOPUS_SUP_CMD_REMOVE) {
            rc = CANOPUS_RESULT_DISALLOWED;
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            break;
        }
        canopus_memcpy(&previous, &sup->modules[slot], sizeof(previous));
        switch (op) {
        case CANOPUS_SUP_CMD_ENABLE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_ENABLED;
            sup->modules[slot].state = CANOPUS_STATE_ENABLED;
            sup->modules[slot].starts = 0u;
            if ((int32_t)sup->modules[slot].activation_error == CANOPUS_SUP_ERR_UNSTABLE ||
                (int32_t)sup->modules[slot].activation_error == CANOPUS_SUP_ERR_UPDATE_FALLBACK) {
                sup->modules[slot].activation_error = 0u;
            }
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        case CANOPUS_SUP_CMD_DISABLE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_DISABLED;
            sup->modules[slot].state = sup_module_loaded(sup->modules[slot].state)
                ? CANOPUS_STATE_DISABLED_NEXT_BOOT : CANOPUS_STATE_DISABLED;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        case CANOPUS_SUP_CMD_REMOVE:
            sup->modules[slot].intent = CANOPUS_SUP_INTENT_REMOVE;
            sup->modules[slot].state = CANOPUS_STATE_REMOVE_PENDING;
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        default:
            rc = sup_plan_rollback(sup, (uint32_t)slot);
            break;
        }
        if (rc == CANOPUS_RESULT_REBOOT_REQUIRED ||
            (op == CANOPUS_SUP_CMD_ROLLBACK && rc == CANOPUS_RESULT_COMPLETED)) {
            int persist_rc;
            sup_refresh_flags(sup, (uint32_t)slot);
            persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                canopus_memcpy(&sup->modules[slot], &previous, sizeof(previous));
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            } else if (previous.state != sup->modules[slot].state ||
                       previous.intent != sup->modules[slot].intent ||
                       previous.flags != sup->modules[slot].flags ||
                       previous.activation_error != sup->modules[slot].activation_error) {
                canopus_supervisor_module_changed(sup, (uint32_t)slot);
            }
        }
        break;
    }

    case CANOPUS_SUP_CMD_ACTIVATE: {
        struct canopus_sup_module_v1 *module;
        slot = (int32_t)arg0;
        if (slot < 0 || (uint32_t)slot >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot].state == 0u) {
            sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
            rc = CANOPUS_RESULT_DISALLOWED;
            break;
        }
        module = &sup->modules[slot];
        /* A disable/enable round trip rewrites the state word while the code
         * stays resident (enable never unloads), so the record reads ENABLED
         * for a module that is already running. Loading it again fails with
         * "already inserted" and marks a live module failed, so report the
         * resident state instead. */
        if (module->descriptor != 0 &&
            module->state == CANOPUS_STATE_ENABLED &&
            module->intent == CANOPUS_SUP_INTENT_ENABLED) {
            module->state = module->lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE
                                ? CANOPUS_STATE_ACTIVE
                                : CANOPUS_STATE_BOOT_RESIDENT;
            module->activation_error = 0u;
            rc = CANOPUS_RESULT_COMPLETED;
            canopus_supervisor_module_changed(sup, (uint32_t)slot);
            if (canopus_supervisor_save_registry(sup) != 0) {
                sup->error_code = CANOPUS_SUP_ERR_REGISTRY;
                rc = CANOPUS_RESULT_FAILED;
            }
            break;
        }
        if ((module->state == CANOPUS_STATE_INSTALLED ||
             module->state == CANOPUS_STATE_ENABLED) &&
            module->intent == CANOPUS_SUP_INTENT_ENABLED) {
            int st = sup->platform != 0 && sup->platform->load_module != 0
                         ? sup_load_slot(sup, (uint32_t)slot)
                         : -1;
            if (st < 0) {
                module->state = CANOPUS_STATE_FAILED;
                module->activation_error = (uint32_t)st;
                /* Keep a specific diagnostic the platform already recorded for
                 * this command (error_code was cleared when it started). */
                if (sup->error_code == CANOPUS_SUP_ERR_NONE) {
                    sup->error_code = CANOPUS_SUP_ERR_LOAD;
                }
                rc = CANOPUS_RESULT_FAILED;
            } else {
                module->state = (uint32_t)st;
                rc = st == CANOPUS_STATE_READY
                         ? sup_activate_module(sup, module)
                         : CANOPUS_RESULT_COMPLETED;
            }
        } else {
            rc = sup_activate_module(sup, module);
        }
        if (rc == CANOPUS_RESULT_COMPLETED && module->descriptor != 0 &&
            (module->descriptor->flags & CANOPUS_FLAG_HAS_NATIVE_APP) != 0u) {
            int publish_rc = canopus_supervisor_ui_pump_ensure(sup);
            if (publish_rc == 0) {
                module->native_app_stage = 1u;
            } else {
                module->activation_error = (uint32_t)publish_rc;
                sup->error_code = (uint32_t)publish_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        if (rc == CANOPUS_RESULT_COMPLETED ||
            rc == CANOPUS_RESULT_FAILED) {
            canopus_supervisor_module_changed(sup, (uint32_t)slot);
            int persist_rc = canopus_supervisor_save_registry(sup);
            if (persist_rc != 0) {
                sup->error_code = persist_rc;
                rc = CANOPUS_RESULT_FAILED;
            }
        }
        break;
    }

    case CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT:
        rc = canopus_supervisor_activate_restored_modules(sup) == 0
                 ? CANOPUS_RESULT_COMPLETED
                 : CANOPUS_RESULT_FAILED;
        if (rc == CANOPUS_RESULT_FAILED &&
            sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_LOAD;
        }
        break;

    case CANOPUS_SUP_CMD_SCAN:
        rc = canopus_supervisor_scan_packages(sup);
        break;

    case CANOPUS_SUP_CMD_OPEN_MANAGER:
        rc = (sup->platform != 0 && sup->platform->open_manager != 0)
                 ? (sup->platform->open_manager(sup->platform_cookie) == 0
                        ? CANOPUS_RESULT_COMPLETED : CANOPUS_RESULT_FAILED)
                 : CANOPUS_RESULT_DISALLOWED;
        break;

    case CANOPUS_SUP_CMD_ENTER_SAFE_MODE:
        sup_enter_safe_mode(sup, CANOPUS_SAFE_MODE_USER_REQUESTED);
        sup->exit_pending = 0u;
        if (canopus_supervisor_save_registry(sup) != 0) {
            sup->error_code = CANOPUS_SUP_ERR_REGISTRY;
            rc = CANOPUS_RESULT_FAILED;
        } else {
            rc = CANOPUS_RESULT_COMPLETED;
        }
        break;

    case CANOPUS_SUP_CMD_EXIT_SAFE_MODE: {
        struct {
            uint32_t state;
            uint32_t intent;
            uint32_t flags;
            uint8_t starts;
            uint8_t pending_switch;
        } previous[CANOPUS_SUP_MODULE_SLOTS];
        uint32_t previous_crashes = sup->crash_counter;
        uint32_t previous_unfinished = sup->boot_unfinished;
        uint32_t previous_state = sup->boot_state;
        uint32_t previous_exit = sup->exit_pending;
        uint32_t i;
        int persist_rc;
        if (!sup->safe_mode) {
            sup->error_code = CANOPUS_SUP_ERR_NOT_SAFE_MODE;
            rc = CANOPUS_RESULT_DISALLOWED;
            break;
        }
        if (arg0 > CANOPUS_SAFE_EXIT_ROLLBACK_ALL) {
            sup->error_code = CANOPUS_SUP_ERR_BAD_COMMAND;
            rc = CANOPUS_RESULT_DISALLOWED;
            break;
        }
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            const struct canopus_sup_module_v1 *m = &sup->modules[i];
            previous[i].state = m->state;
            previous[i].intent = m->intent;
            previous[i].flags = m->flags;
            previous[i].starts = m->starts;
            previous[i].pending_switch = m->pending_switch;
        }
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            struct canopus_sup_module_v1 *m = &sup->modules[i];
            if (m->state == 0u) continue;
            m->starts = 0u;
            if (arg0 == CANOPUS_SAFE_EXIT_DISABLE_ALL &&
                m->intent == CANOPUS_SUP_INTENT_ENABLED) {
                m->intent = CANOPUS_SUP_INTENT_DISABLED;
                m->state = sup_module_loaded(m->state)
                               ? CANOPUS_STATE_DISABLED_NEXT_BOOT : CANOPUS_STATE_DISABLED;
            } else if (arg0 == CANOPUS_SAFE_EXIT_ROLLBACK_ALL &&
                       m->intent != CANOPUS_SUP_INTENT_REMOVE) {
                rc = sup_plan_rollback(sup, i);
                if (rc == CANOPUS_RESULT_FAILED) goto exit_restore;
                sup_refresh_flags(sup, i);
            }
        }
        sup->error_code = CANOPUS_SUP_ERR_NONE;
        sup->exit_pending = 1u;
        sup->crash_counter = 0u;
        sup->boot_unfinished = 0u;
        sup->boot_state = CANOPUS_BOOT_OK;
        persist_rc = canopus_supervisor_save_registry(sup);
        if (persist_rc == 0) {
            for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
                if (previous[i].state != sup->modules[i].state ||
                    previous[i].intent != sup->modules[i].intent ||
                    previous[i].flags != sup->modules[i].flags) {
                    canopus_supervisor_module_changed(sup, i);
                }
            }
            rc = CANOPUS_RESULT_REBOOT_REQUIRED;
            break;
        }
        sup->error_code = (uint32_t)persist_rc;
        rc = CANOPUS_RESULT_FAILED;
    exit_restore:
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            struct canopus_sup_module_v1 *m = &sup->modules[i];
            m->state = previous[i].state;
            m->intent = previous[i].intent;
            m->flags = previous[i].flags;
            m->starts = previous[i].starts;
            m->pending_switch = previous[i].pending_switch;
        }
        sup->crash_counter = previous_crashes;
        sup->boot_unfinished = previous_unfinished;
        sup->boot_state = previous_state;
        sup->exit_pending = previous_exit;
        break;
    }

    default:
        rc = CANOPUS_RESULT_REJECTED;
        sup->error_code = CANOPUS_SUP_ERR_UNKNOWN_OP;
        break;
    }
    return rc;
}

/* ---- CAN-P0-006: boot markers -------------------------------------- */

void canopus_supervisor_boot_begin(struct canopus_supervisor_v1 *sup,
                                   uint32_t boot_id)
{
    if (sup == 0) {
        return;
    }
    sup->boot_state = CANOPUS_BOOT_BOOTING;
    sup->safe_mode_boot_id = boot_id;
}

void canopus_supervisor_boot_ok(struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return;
    }
    sup_boot_commit_ok(sup);
    if (sup_boot_word(sup) != sup->boot_saved) {
        (void)canopus_supervisor_save_registry(sup);
    }
}

int canopus_supervisor_boot_should_safe_mode(
    const struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return 1; /* a missing supervisor is never a healthy boot */
    }
    if (sup->boot_state == CANOPUS_BOOT_BOOTING) {
        return 1; /* previous boot never committed BOOT_OK */
    }
    if (sup->crash_counter >= CANOPUS_SUP_CRASH_THRESHOLD) {
        return 1;
    }
    return 0;
}

/* CAN-P2-016: saturating crash counter — a long crash loop reports the
 * threshold, never a wrapped counter that silently looks healthy. */
void canopus_supervisor_record_crash(struct canopus_supervisor_v1 *sup)
{
    if (sup == 0) {
        return;
    }
    if (sup->crash_counter < 0xFFFFFFFFu) {
        sup->crash_counter++;
    }
}

uint32_t canopus_supervisor_handle_command(struct canopus_supervisor_v1 *sup,
                                           const uint8_t command[CANOPUS_SUP_COMMAND_SIZE])
{
    uint32_t op, arg0, arg1;
    uint32_t rc;

    if (sup == 0) {
        return CANOPUS_RESULT_REJECTED;
    }
    if (canopus_supervisor_bus_dispatching()) return CANOPUS_RESULT_DISALLOWED;
    if (canopus_supervisor_validate_command(command) != 0) {
        canopus_snapshot_begin(&sup->snap);
        sup->pending_op = 0;
        sup->pending_state = CANOPUS_RESULT_REJECTED;
        sup->error_code = CANOPUS_SUP_ERR_BAD_COMMAND;
        canopus_snapshot_commit(&sup->snap);
        return sup->pending_state;
    }
    op = cmd_word(command, 0);
    arg0 = cmd_word(command, 1);
    arg1 = cmd_word(command, 2);

    /* CAN-P1-003: all state mutation runs under the snapshot protocol so a
     * reader never observes a torn record (sequence odd or begin != end). */
    canopus_snapshot_begin(&sup->snap);
    sup->pending_op = op;
    sup->selected = (int32_t)arg0;
    if (op == CANOPUS_SUP_CMD_QUERY && arg0 == CANOPUS_SUP_DIAG_QUERY_MAGIC) {
        /* Read the last mutation result without erasing its failure code. */
        sup->pending_state = CANOPUS_RESULT_COMPLETED;
        canopus_snapshot_commit(&sup->snap);
        return sup->pending_state;
    }
    /* CAN-P1-008: clear this command's error at the start; failure paths
     * set a stable code and it is never cleared again at the tail. */
    sup->error_code = CANOPUS_SUP_ERR_NONE;
    rc = sup_dispatch(sup, op, arg0, arg1, 0);
    sup->pending_state = rc;
    canopus_snapshot_commit(&sup->snap);
    canopus_supervisor_module_events_flush(sup);
    return rc;
}

/* ---- CAN-P0-008: v2 transport handling ----------------------------- */

static uint32_t v2_to_sup_cmd(uint32_t cmd)
{
    switch (cmd) {
    case CANOPUS_CMD_ECHO:
    case CANOPUS_CMD_QUERY_MODULE:
    case CANOPUS_CMD_QUERY_DEVICE:
        return CANOPUS_SUP_CMD_QUERY;
    case CANOPUS_CMD_INSTALL:
        return CANOPUS_SUP_CMD_INSTALL;
    case CANOPUS_CMD_ENABLE:
        return CANOPUS_SUP_CMD_ENABLE;
    case CANOPUS_CMD_DISABLE:
        return CANOPUS_SUP_CMD_DISABLE;
    case CANOPUS_CMD_REMOVE:
        return CANOPUS_SUP_CMD_REMOVE;
    case CANOPUS_CMD_UPDATE:
        return CANOPUS_SUP_CMD_UPDATE;
    case CANOPUS_CMD_ROLLBACK:
        return CANOPUS_SUP_CMD_ROLLBACK;
    case CANOPUS_CMD_ENTER_SAFE_MODE:
        return CANOPUS_SUP_CMD_ENTER_SAFE_MODE;
    case CANOPUS_CMD_EXIT_SAFE_MODE:
        return CANOPUS_SUP_CMD_EXIT_SAFE_MODE;
    case CANOPUS_CMD_ACTIVATE:
        return CANOPUS_SUP_CMD_ACTIVATE;
    case CANOPUS_CMD_SCAN:
        return CANOPUS_SUP_CMD_SCAN;
    case CANOPUS_CMD_NATIVE_STAGE:
        return CANOPUS_SUP_CMD_INSTALL;
    case CANOPUS_CMD_RESTORE_AFTER_BOOT:
        return CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT;
    case CANOPUS_CMD_OPEN_MANAGER:
        return CANOPUS_SUP_CMD_OPEN_MANAGER;
    default:
        return 0; /* unknown command */
    }
}

static int v2_op_needs_slot(uint32_t cmd)
{
    return cmd == CANOPUS_CMD_ENABLE || cmd == CANOPUS_CMD_DISABLE ||
           cmd == CANOPUS_CMD_REMOVE || cmd == CANOPUS_CMD_UPDATE ||
           cmd == CANOPUS_CMD_ROLLBACK || cmd == CANOPUS_CMD_ACTIVATE;
}

static uint32_t wire_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* NUL-aware bounded compare of a slot's stored module id against a request
 * payload (the Manager pads to CANOPUS_SUP_MODULE_ID_MAX with NULs). */
static int sup_id_eq(const uint8_t *a, const uint8_t *b, uint32_t b_len)
{
    uint32_t i;
    for (i = 0; i < CANOPUS_SUP_MODULE_ID_MAX; i++) {
        uint8_t av = a[i];
        uint8_t bv = (i < b_len) ? b[i] : 0u;
        if (av != bv) {
            return 0;
        }
        if (av == 0) {
            return 1;
        }
    }
    return 0; /* stored id not NUL-terminated within the window */
}

static int sup_slot_from_module_id(const struct canopus_supervisor_v1 *sup,
                                   const void *payload, uint32_t payload_len)
{
    uint32_t i;
    if (payload == 0 || payload_len == 0 || payload_len > CANOPUS_SUP_MODULE_ID_MAX) {
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state != 0 &&
            sup_id_eq(sup->modules[i].module_id, (const uint8_t *)payload,
                      payload_len)) {
            return (int)i;
        }
    }
    return -1;
}

/* CAN-P0-003: an INSTALL stage token is a bounded basename, never an
 * arbitrary path. Only [a-z0-9._-], NUL-terminated within the bound, and
 * never ".", ".." or a leading/trailing "."/"-" (a "." component could
 * escape a future open-at-root resolution). */
static int sup_stage_token_ok(const void *payload, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)payload;
    uint32_t i, n = 0;
    if (payload == 0 || len == 0 || len > CANOPUS_SUP_STAGE_TOKEN_MAX) {
        return 0;
    }
    for (i = 0; i < len; i++) {
        if (p[i] == 0) {
            n = i;
            break;
        }
    }
    if (i == len) {
        return 0; /* no NUL within the bound */
    }
    if (n == 0) {
        return 0; /* empty token */
    }
    /* "." and ".." are traversal components, never valid basenames */
    if ((n == 1 && p[0] == '.') ||
        (n == 2 && p[0] == '.' && p[1] == '.')) {
        return 0;
    }
    /* leading/trailing separators and hidden names are not valid boundaries */
    if (p[0] == '.' || p[0] == '-' || p[n - 1] == '.' || p[n - 1] == '-') {
        return 0;
    }
    for (i = 0; i < n; i++) {
        uint8_t c = p[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

int canopus_supervisor_handle_v2_request(struct canopus_supervisor_v1 *sup,
                                         const struct canopus_proto_request_v1 *req,
                                         const void *payload,
                                         struct canopus_proto_response_v1 *resp,
                                         uint32_t *opcode)
{
    uint32_t op, slot_arg;
    uint32_t rc;
    int track_request;

    if (sup == 0 || req == 0 || resp == 0 || opcode == 0) {
        return -1;
    }
    *opcode = req->command;
    if (canopus_supervisor_bus_dispatching()) {
        canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_DISALLOWED, 0u);
        return 0;
    }
    if (req->command >= CANOPUS_CMD_QUERY_BOOT && req->command <= CANOPUS_CMD_START_BOOT) {
        int status;
        if ((req->command == CANOPUS_CMD_SET_BOOT &&
             (payload == 0 || req->payload_size != 4u || wire_u32(payload) > 1u)) ||
            (req->command != CANOPUS_CMD_SET_BOOT && req->payload_size != 0u)) {
            canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_DISALLOWED, 0u);
            return 0;
        }
        canopus_snapshot_begin(&sup->snap);
        if (req->command == CANOPUS_CMD_QUERY_BOOT)
            status = canopus_supervisor_boot_query(sup, &sup->boot_persistence);
        else if (req->command == CANOPUS_CMD_SET_BOOT)
            status = canopus_supervisor_boot_set(sup, (int)wire_u32(payload));
        else
            status = canopus_supervisor_boot_start(sup);
        if (status != 0) sup->error_code = (uint32_t)status;
        rc = status != 0 ? CANOPUS_RESULT_FAILED
             : req->command == CANOPUS_CMD_SET_BOOT ? CANOPUS_RESULT_REBOOT_REQUIRED
                                                   : CANOPUS_RESULT_COMPLETED;
        canopus_snapshot_commit(&sup->snap);
        canopus_proto_response_init(resp, req->request_id, rc, 0u);
        return 0;
    }
    /* The staging commands, as bits counted from STAGE_BEGIN. Written out because the compiler
     * folds the two range tests into (1 << command) & 0x1C078000, a word inside Band 10 Pro's ROM
     * range that the verifier rightly refuses as an embedded firmware address. */
    if (req->command - (uint32_t)CANOPUS_CMD_STAGE_BEGIN < 32u &&
        ((((1u << (CANOPUS_CMD_STAGE_ABORT - CANOPUS_CMD_STAGE_BEGIN + 1)) - 1u) |
          (((1u << (CANOPUS_CMD_STAGE_RES_FINISH - CANOPUS_CMD_STAGE_RES_BEGIN + 1)) - 1u)
           << (CANOPUS_CMD_STAGE_RES_BEGIN - CANOPUS_CMD_STAGE_BEGIN))) >>
         (req->command - (uint32_t)CANOPUS_CMD_STAGE_BEGIN)) & 1u) {
        rc = canopus_supervisor_upload(sup, req->command, payload, req->payload_size);
        canopus_proto_response_init(resp, req->request_id, rc, 0u);
        return 0;
    }
    op = v2_to_sup_cmd(req->command);
    if (op == 0) {
        canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_REJECTED, 0);
        return 0;
    }
    /* CAN-P0-003: INSTALL must carry a bounded stage token, never a path */
    if (req->command == CANOPUS_CMD_INSTALL &&
        !sup_stage_token_ok(payload, req->payload_size)) {
        canopus_proto_response_init(resp, req->request_id, CANOPUS_RESULT_DISALLOWED, 0);
        return 0;
    }
    if (req->command == CANOPUS_CMD_NATIVE_STAGE) {
        if (payload == 0 || req->payload_size != 4u ||
            wire_u32((const uint8_t *)payload) > 2u) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
    }
    if (req->command == CANOPUS_CMD_EXIT_SAFE_MODE) {
        if (payload == 0 || req->payload_size != 4u ||
            wire_u32((const uint8_t *)payload) > CANOPUS_SAFE_EXIT_ROLLBACK_ALL) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
    }
    if (req->command == CANOPUS_CMD_QUERY_MODULE) {
        if (payload == 0 || req->payload_size != 4u) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
        slot_arg = wire_u32((const uint8_t *)payload);
        if (slot_arg >= CANOPUS_SUP_MODULE_SLOTS ||
            sup->modules[slot_arg].state == 0u) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
    } else if (v2_op_needs_slot(req->command)) {
        int found = sup_slot_from_module_id(sup, payload, req->payload_size);
        if (found < 0) {
            canopus_proto_response_init(resp, req->request_id,
                                        CANOPUS_RESULT_DISALLOWED, 0);
            return 0;
        }
        slot_arg = (uint32_t)found;
    } else {
        slot_arg = 0;
    }
    /* Read-only queries complete synchronously and do not consume retained
     * pending slots. Mutations remain observable until an explicit ACK path is
     * negotiated. */
    track_request = req->command != CANOPUS_CMD_ECHO &&
                    req->command != CANOPUS_CMD_QUERY_DEVICE &&
                    req->command != CANOPUS_CMD_QUERY_MODULE;
    if (track_request &&
        canopus_pending_accept(&sup->pending, req->request_id,
                               req->command) != 0) {
        canopus_proto_response_init(resp, req->request_id,
                                    CANOPUS_RESULT_REJECTED, 0);
        return 0;
    }
    /* run under the snapshot protocol; INSTALL receives the request payload
     * as the package path, other ops use the resolved slot */
    canopus_snapshot_begin(&sup->snap);
    sup->pending_op = op;
    sup->selected = (int32_t)slot_arg;
    if (req->command != CANOPUS_CMD_QUERY_DEVICE &&
        req->command != CANOPUS_CMD_QUERY_MODULE &&
        req->command != CANOPUS_CMD_ECHO) {
        sup->error_code = CANOPUS_SUP_ERR_NONE;
    }
    rc = sup_dispatch(sup, op,
                      req->command == CANOPUS_CMD_INSTALL ? 0u
                      : req->command == CANOPUS_CMD_NATIVE_STAGE ||
                        req->command == CANOPUS_CMD_EXIT_SAFE_MODE
                            ? wire_u32((const uint8_t *)payload)
                            : slot_arg,
                      0,
                      req->command == CANOPUS_CMD_INSTALL
                          ? (const char *)payload : 0);
    sup->pending_state = rc;
    canopus_snapshot_commit(&sup->snap);
    if (track_request) {
        canopus_pending_set_error(&sup->pending, req->request_id,
                                  sup->error_code);
        (void)canopus_pending_finish(&sup->pending, req->request_id, rc);
        /* CPC2 operations are synchronous today: the final result is returned
         * in this response, so retaining the terminal record would consume
         * one of the bounded request slots forever. Future genuinely async
         * operations may retain their record until an explicit ACK path. */
        (void)canopus_pending_ack(&sup->pending, req->request_id);
    }
    canopus_proto_response_init(resp, req->request_id, rc, 0);
    canopus_supervisor_module_events_flush(sup);
    return 0;
}

int canopus_supervisor_local_transport(
    const struct canopus_proto_request_v1 *req, const void *payload,
    struct canopus_proto_response_v1 *resp, void *cookie)
{
    struct canopus_supervisor_v1 *sup =
        (struct canopus_supervisor_v1 *)cookie;
    uint32_t opcode;

    if (sup == 0 || req == 0 || resp == 0 ||
        req->magic != CANOPUS_PROTO_MAGIC ||
        req->struct_size != CANOPUS_PROTO_REQUEST_SIZE ||
        req->abi_major != CANOPUS_ABI_MAJOR ||
        req->abi_minor > CANOPUS_ABI_MINOR || req->request_id == 0u ||
        req->payload_size > CANOPUS_PROTO_MAX_PAYLOAD ||
        req->flags != 0u || (req->payload_size != 0u && payload == 0)) {
        return -1;
    }
    return canopus_supervisor_handle_v2_request(sup, req, payload, resp,
                                                &opcode);
}

static void put_wire_u32(uint8_t *out, uint32_t offset, uint32_t value)
{
    out[offset] = (uint8_t)value;
    out[offset + 1u] = (uint8_t)(value >> 8);
    out[offset + 2u] = (uint8_t)(value >> 16);
    out[offset + 3u] = (uint8_t)(value >> 24);
}

static uint32_t render_v2_query_payload(
    const struct canopus_supervisor_v1 *sup,
    const struct canopus_proto_request_v1 *req,
    const struct canopus_proto_response_v1 *resp,
    uint8_t out[CANOPUS_PROTO_MAX_PAYLOAD])
{
    if (resp->result_state != CANOPUS_RESULT_COMPLETED) {
        return 0u;
    }
    if (req->command == CANOPUS_CMD_QUERY_BOOT) {
        put_wire_u32(out, 0u, sup->boot_persistence);
        return 4u;
    }
    if (req->command == CANOPUS_CMD_QUERY_DEVICE) {
        canopus_memset(out, 0, CANOPUS_QUERY_DEVICE_SIZE);
        put_wire_u32(out, 0u, CANOPUS_QUERY_DEVICE_MAGIC);
        put_wire_u32(out, 4u, CANOPUS_QUERY_DEVICE_SIZE);
        put_wire_u32(out, 8u, sup->framework_revision);
        put_wire_u32(out, 12u, sup->safe_mode);
        put_wire_u32(out, 16u, sup->module_count);
        put_wire_u32(out, 20u, sup->safe_mode_reason);
        put_wire_u32(out, 24u, (uint32_t)sup->error_code);
        put_wire_u32(out, 28u, sup->flags);
        put_wire_u32(out, 32u, sup->scan_summary);
        if (sup->platform != 0 && sup->platform->target_id != 0) {
            canopus_memcpy(out + 36u, sup->platform->target_id,
                canopus_strnlen(sup->platform->target_id, 47u));
        }
        if (sup->platform != 0 && sup->platform->l2_request_slots != 0) {
            put_wire_u32(out, 84u, sup->platform->l2_request_slots(sup->platform_cookie));
        }
        return CANOPUS_QUERY_DEVICE_SIZE;
    }
    if (req->command == CANOPUS_CMD_QUERY_MODULE &&
        sup->selected >= 0 &&
        (uint32_t)sup->selected < CANOPUS_SUP_MODULE_SLOTS) {
        const struct canopus_sup_module_v1 *module =
            &sup->modules[(uint32_t)sup->selected];
        canopus_memset(out, 0, CANOPUS_QUERY_MODULE_SIZE);
        put_wire_u32(out, 0u, CANOPUS_QUERY_MODULE_MAGIC);
        put_wire_u32(out, 4u, CANOPUS_QUERY_MODULE_SIZE);
        put_wire_u32(out, 8u, (uint32_t)sup->selected);
        put_wire_u32(out, 12u, module->state);
        put_wire_u32(out, 16u, module->lifecycle_class);
        put_wire_u32(out, 20u, module->version);
        put_wire_u32(out, 24u, module->flags);
        put_wire_u32(out, 60u, module->activation_error);
        canopus_memcpy(out + 28u, module->module_id,
                       CANOPUS_SUP_MODULE_ID_MAX);
        return CANOPUS_QUERY_MODULE_SIZE;
    }
    return 0u;
}

int canopus_supervisor_render_status(const struct canopus_supervisor_v1 *sup,
                                     uint8_t out[CANOPUS_SUP_STATUS_SIZE])
{
    uint32_t i;
    uint32_t seq_begin;
    uint32_t module_error = 0u;
    if (sup == 0 || out == 0) {
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
    PUT32(CANOPUS_SUP_STATUS_SCAN_OFF, sup->scan_summary);
    /* CAN-P1-003: snapshot begin read once; the end is re-read after the
     * payload so a concurrent mutation leaves begin != end (or odd). */
    seq_begin = sup->snap.sequence;
    PUT32(CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF, seq_begin);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (module_error == 0u && sup->modules[i].activation_error != 0u) {
            module_error = sup->modules[i].activation_error;
        }
    }
    PUT32(CANOPUS_SUP_STATUS_MODULE_ERROR_OFF, module_error);
    canopus_manager_target_render_diagnostics(out + 48u);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        uint32_t o = 128u + i * CANOPUS_SUP_MODULE_SLOT_STRIDE;
        const struct canopus_sup_module_v1 *m = &sup->modules[i];
        PUT32(o + 0, m->state);
        PUT32(o + 4, m->lifecycle_class);
        PUT32(o + 8, m->version);
        PUT32(o + 12, m->flags);
    }
    PUT32(CANOPUS_SUP_STATUS_SEQ_END_OFF, sup->snap.sequence);
#undef PUT32
    return 0;
}

int32_t canopus_supervisor_device_read(struct canopus_supervisor_v1 *sup,
                                       void *buffer, uint32_t count)
{
    if (sup == 0 || buffer == 0) {
        return -1;
    }
    /* CPC2 only: the read returns the response of the last v2 write. */
    if (sup->last_kind != 1 || count < sup->v2_response_len ||
        sup->v2_response_len == 0) {
        return -1;
    }
    canopus_memcpy(buffer, sup->v2_response_buf, sup->v2_response_len);
    return (int32_t)sup->v2_response_len;
}

int32_t canopus_supervisor_device_write(struct canopus_supervisor_v1 *sup,
                                        const void *buffer, uint32_t count)
{
    const uint8_t *b;
    uint32_t magic;
    if (sup == 0 || buffer == 0) {
        return -1;
    }
    b = (const uint8_t *)buffer;
    /* CPC2 is the only wire format (ADR-CAN-004). Unknown magic or a
     * wrong-size frame is a negative error and never consumed. */
    if (count >= CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        struct canopus_proto_request_v1 req;
        struct canopus_proto_response_v1 resp;
        uint8_t response_payload[CANOPUS_PROTO_MAX_PAYLOAD];
        uint32_t response_payload_len;
        uint32_t poff = 0;
        int rc;
        magic = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
                ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
        if (magic == CANOPUS_TRANSPORT_V2_MAGIC) {
            if (canopus_transport_v2_decode_request(b, count, &req, &poff) != 0) {
                return -1; /* malformed v2 record: reject, consume nothing */
            }
            sup->last_kind = 1;
            rc = canopus_supervisor_handle_v2_request(sup, &req, b + poff,
                                                      &resp, &magic);
            if (rc != 0) {
                return -1;
            }
            response_payload_len = render_v2_query_payload(
                sup, &req, &resp, response_payload);
            resp.payload_size = response_payload_len;
            sup->v2_response_len = (uint32_t)canopus_transport_v2_encode_response(
                &resp, magic,
                response_payload_len != 0u ? response_payload : 0,
                response_payload_len, sup->v2_response_buf,
                sizeof(sup->v2_response_buf));
            if (sup->v2_response_len == (uint32_t)-1) {
                return -1;
            }
            return (int32_t)count; /* whole record consumed */
        }
    }
    return -1; /* unknown magic / malformed frame */
}

/* Public watchfaces use /canopus/install through ordinary Lua io.open.
 * This endpoint never accepts CPC1, registration pointers, management commands
 * or a payload-free framework INSTALL. Signed-package verification stays in
 * the existing platform stage_package implementation. */
int32_t canopus_supervisor_installer_write(struct canopus_supervisor_v1 *sup,
                                          const void *buffer, uint32_t count)
{
    struct canopus_proto_request_v1 req;
    struct canopus_proto_response_v1 resp;
    uint32_t offset = 0u, opcode = 0u;
    uint8_t diagnostic[4];
    int encoded;
    if (sup == 0 || buffer == 0) return -1;
    sup->installer_response_len = 0u;
    if (canopus_transport_v2_decode_request(buffer, count, &req, &offset) != 0 ||
        req.command != CANOPUS_CMD_INSTALL || req.flags != 0u ||
        !sup_stage_token_ok((const uint8_t *)buffer + offset, req.payload_size)) {
        return -1;
    }
    if (canopus_supervisor_handle_v2_request(sup, &req,
            (const uint8_t *)buffer + offset, &resp, &opcode) != 0) return -1;
    put_wire_u32(diagnostic, 0u, sup->error_code);
    resp.payload_size = sizeof(diagnostic);
    encoded = canopus_transport_v2_encode_response(&resp, opcode, diagnostic,
        sizeof(diagnostic), sup->installer_response, sizeof(sup->installer_response));
    if (encoded != (int)sizeof(sup->installer_response)) return -1;
    sup->installer_response_len = (uint32_t)encoded;
    return (int32_t)count;
}

int32_t canopus_supervisor_installer_read(struct canopus_supervisor_v1 *sup,
                                         void *buffer, uint32_t count)
{
    if (sup == 0 || buffer == 0 || sup->installer_response_len == 0u ||
        count < sup->installer_response_len) return -1;
    canopus_memcpy(buffer, sup->installer_response, sup->installer_response_len);
    return (int32_t)sup->installer_response_len;
}

/* WearPacket L2 packet handler (ADR-CAN-008).
 * Processes an incoming CPC2 request from L2Channel::Canopus (0x43) and
 * writes the resulting CPC2 response directly into resp_buf. */
int32_t canopus_supervisor_handle_l2_packet(struct canopus_supervisor_v1 *sup,
                                            const uint8_t *l2_payload,
                                            uint32_t l2_payload_len,
                                            uint8_t *resp_buf,
                                            uint32_t resp_cap)
{
    struct canopus_proto_request_v1 req;
    struct canopus_proto_response_v1 resp;
    uint8_t response_payload[CANOPUS_PROTO_MAX_PAYLOAD];
    uint32_t response_payload_len;
    uint32_t poff = 0u;
    uint32_t opcode = 0u;
    int rc;
    int enc_len;

    if (sup == 0 || l2_payload == 0 || resp_buf == 0 ||
        l2_payload_len < CANOPUS_TRANSPORT_V2_HEADER_SIZE) {
        return -1;
    }

    if (canopus_transport_v2_decode_request(l2_payload, l2_payload_len, &req, &poff) != 0) {
        return -1;
    }

    /* CanopusConnect shares the stream but not the Manager's state: no pending
     * slot, status snapshot, error code or last_kind is touched. */
    if (req.command == CANOPUS_CMD_CONNECT_SEND || req.command == CANOPUS_CMD_CONNECT_LISTEN) {
        uint8_t status;
        canopus_proto_response_init(&resp, req.request_id,
            canopus_supervisor_connect_request(&req, l2_payload + poff, &status), 1u);
        enc_len = canopus_transport_v2_encode_response(&resp, req.command, &status, 1u,
                                                       resp_buf, resp_cap);
        return enc_len < 0 ? -1 : (int32_t)enc_len;
    }

    sup->last_kind = 1;
    rc = canopus_supervisor_handle_v2_request(sup, &req, l2_payload + poff,
                                              &resp, &opcode);
    if (rc != 0) {
        return -1;
    }

    response_payload_len = render_v2_query_payload(sup, &req, &resp, response_payload);
    resp.payload_size = response_payload_len;

    enc_len = canopus_transport_v2_encode_response(
        &resp, opcode,
        response_payload_len != 0u ? response_payload : 0,
        response_payload_len, resp_buf, resp_cap);

    if (enc_len < 0) {
        return -1;
    }

    return (int32_t)enc_len;
}

/* Host convenience: record a newly installed module into a slot.
 * `module_id` becomes the stable identity for v2 per-module commands. */
int canopus_supervisor_add_module(struct canopus_supervisor_v1 *sup,
                                  uint32_t lifecycle_class,
                                  uint32_t version,
                                  const char *module_id)
{
    uint32_t i;
    uint32_t n;
    if (sup == 0 || canopus_supervisor_bus_dispatching()) {
        return -1;
    }
    if (!module_lifecycle_ok(lifecycle_class)) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_CLASS;
        return -1;
    }
    if (module_id == 0) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
        return -1;
    }
    n = (uint32_t)canopus_strnlen(module_id, CANOPUS_SUP_MODULE_ID_MAX);
    if (n >= CANOPUS_SUP_MODULE_ID_MAX || n == 0) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT; /* too long or empty */
        return -1;
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state == 0) {
            sup->modules[i].state = CANOPUS_STATE_INSTALLED;
            sup->modules[i].lifecycle_class = lifecycle_class;
            sup->modules[i].version = version;
            sup->modules[i].flags = 0u;
            /* Disabled by default: nothing is ever loaded at install. */
            sup->modules[i].intent = CANOPUS_SUP_INTENT_DISABLED;
            canopus_memset(sup->modules[i].module_id, 0,
                           sizeof(sup->modules[i].module_id));
            canopus_memcpy(sup->modules[i].module_id, module_id, n);
            sup->modules[i].module_id[n] = 0;
            sup->module_count++;
            return (int)i;
        }
    }
    sup->error_code = CANOPUS_SUP_ERR_TABLE_FULL;
    return -1;
}

int32_t canopus_supervisor_native_app_id(struct canopus_supervisor_v1 *sup,
                                        uint32_t slot)
{
    struct canopus_sup_module_v1 *module;
    uint32_t id, i;
    if (sup == 0 || slot >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->modules[slot].state == 0u) return CANOPUS_SVC_ERR_STATE;
    module = &sup->modules[slot];
    if (module->native_app_id != 0u) return module->native_app_id;
    for (id = CANOPUS_SUP_NATIVE_APP_FIRST_ID;
         id < CANOPUS_SUP_NATIVE_APP_FIRST_ID + CANOPUS_SUP_NATIVE_APP_IDS; id++) {
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            if (sup->modules[i].state != 0u && sup->modules[i].native_app_id == id) break;
        }
        if (i != CANOPUS_SUP_MODULE_SLOTS) continue;
        if (sup->platform != 0 && sup->platform->native_app_id_in_use != 0) {
            int used = sup->platform->native_app_id_in_use(sup->platform_cookie, (uint16_t)id);
            if (used < 0) return CANOPUS_SVC_ERR_STATE;
            if (used != 0) continue;
        }
        module->native_app_id = (uint8_t)id;
        if (canopus_supervisor_save_registry(sup) != 0) {
            /* No firmware publication may precede durable ownership. */
            module->native_app_id = 0u;
            return CANOPUS_SVC_ERR_IO;
        }
        return (int32_t)id;
    }
    return CANOPUS_SVC_ERR_NOMEM;
}

static int sup_publish_native_apps(struct canopus_supervisor_v1 *sup,
                                   uint32_t stage, uint32_t first, uint32_t end)
{
    uint32_t i;
    int failed = 0;
    for (i = first; i < end; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        uint32_t legacy_end;
        uint32_t staged_end;
        uint32_t previous_error = module->activation_error;
        int rc;
        if (!sup_module_loaded(module->state) || module->descriptor == 0 ||
            module->descriptor->abi_major != CANOPUS_ABI_MAJOR ||
            (module->descriptor->flags & CANOPUS_FLAG_HAS_NATIVE_APP) == 0u) {
            continue;
        }
        if (stage == 2u && module->native_app_status == CANOPUS_SUP_NATIVE_APP_PENDING) {
            /* Bootstrap callers may request stage 2 before the queue drains. */
            module->native_app_stage = 2u;
            continue;
        }
        if (stage == 2u && module->native_app_status == CANOPUS_SUP_NATIVE_APP_FAILED) {
            rc = CANOPUS_SUP_ERR_LAUNCHER;
            goto publication_result;
        }
        legacy_end =
            (uint32_t)offsetof(struct canopus_module_descriptor_v1,
                               publish_native_app) +
            (uint32_t)sizeof(module->descriptor->publish_native_app);
        staged_end =
            (uint32_t)offsetof(struct canopus_module_descriptor_v1,
                               publish_native_app_stage) +
            (uint32_t)sizeof(module->descriptor->publish_native_app_stage);
        if (module->descriptor->struct_size >= staged_end &&
            module->descriptor->publish_native_app_stage != 0) {
            rc = module->descriptor->publish_native_app_stage(
                canopus_supervisor_context(sup, i), stage);
        } else if (stage == 1u &&
                   module->descriptor->struct_size >= legacy_end &&
                   module->descriptor->publish_native_app != 0) {
            rc = module->descriptor->publish_native_app(
                canopus_supervisor_context(sup, i));
        } else {
            continue;
        }
publication_result:
        if (rc != 0) {
            module->activation_error = (uint32_t)rc;
            failed = -1;
        } else {
            module->activation_error = 0u;
        }
        if (module->activation_error != previous_error) {
            canopus_supervisor_module_changed(sup, i);
        }
    }
    {
        int persist_result = canopus_supervisor_save_registry(sup);
        if (persist_result != 0) {
            /* App callbacks may already have committed; retain the specific
             * storage error and notify the state kept in memory. */
            sup->error_code = (uint32_t)persist_result;
            failed = -1;
        }
    }
    canopus_supervisor_module_events_flush(sup);
    return failed;
}

int canopus_supervisor_publish_native_apps(struct canopus_supervisor_v1 *sup,
                                           uint32_t stage)
{
    if (sup == 0 || (stage != 1u && stage != 2u) ||
        canopus_supervisor_bus_dispatching()) return -1;
    return sup_publish_native_apps(sup, stage, 0u, CANOPUS_SUP_MODULE_SLOTS);
}

void canopus_supervisor_native_apps_poll(struct canopus_supervisor_v1 *sup)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];
        uint32_t stage = module->native_app_stage;
        if (stage == 0u) continue;
        module->native_app_stage = 0u;
        if (sup->safe_mode || !sup_module_loaded(module->state) ||
            module->intent != CANOPUS_SUP_INTENT_ENABLED) continue;
        /* Each heartbeat performs at most one app/page or Launcher mutation.
         * Keep publication independent of the module's own ui->post slot. */
        canopus_snapshot_begin(&sup->snap);
        if (sup_publish_native_apps(sup, stage, i, i + 1u) == 0 && stage == 1u)
            module->native_app_stage = 2u;
        canopus_snapshot_commit(&sup->snap);
        canopus_supervisor_module_events_flush(sup);
        canopus_manager_native_refresh();
        return;
    }
}

static int sup_find_slot_by_id(const struct canopus_supervisor_v1 *sup,
                               const char *token)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state != 0u &&
            sup_fixed_string_equal(sup->modules[i].module_id,
                                   CANOPUS_SUP_MODULE_ID_MAX, token)) {
            return (int)i;
        }
    }
    return -1;
}

int canopus_supervisor_stage_token(struct canopus_supervisor_v1 *sup,
                                   const char *token)
{
    const struct canopus_sup_platform_v1 *pf;
    uint32_t version;
    uint32_t lifecycle;
    int slot;

    if (sup == 0 || token == 0 || sup->platform == 0 || canopus_supervisor_bus_dispatching()) return -1;
    pf = sup->platform;
    if (pf->stage_verify == 0 || pf->stage_commit == 0) {
        sup->error_code = CANOPUS_SUP_ERR_STAGE;
        return -1;
    }
    if (pf->stage_verify(sup->platform_cookie, token, &version, &lifecycle) != 0) {
        if (sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE;
        }
        return -1;
    }
    slot = sup_find_slot_by_id(sup, token);
    if (slot < 0) {
        int added;
        if (pf->stage_commit(sup->platform_cookie, token, 0u) != 0) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE_MOVE;
            return -1;
        }
        added = canopus_supervisor_add_module(sup, lifecycle, version, token);
        if (added < 0) {
            if (sup->error_code == CANOPUS_SUP_ERR_NONE) {
                sup->error_code = CANOPUS_SUP_ERR_STAGE_REGISTER;
            }
            return -1;
        }
        sup_refresh_flags(sup, (uint32_t)added);
        return 0;
    }
    if (sup->modules[slot].state == CANOPUS_STATE_REMOVE_PENDING) {
        sup->error_code = CANOPUS_SUP_ERR_BAD_SLOT;
        return -1;
    }
    if (version <= sup->modules[slot].version) {
        sup->error_code = version == sup->modules[slot].version
                              ? CANOPUS_SUP_ERR_STAGE_DUPLICATE
                              : CANOPUS_SUP_ERR_STAGE_DOWNGRADE;
        return -1;
    }
    if (pf->stage_commit(sup->platform_cookie, token,
                         (uint32_t)(sup->modules[slot].active_gen ^ 1u)) != 0) {
        sup->error_code = CANOPUS_SUP_ERR_STAGE_MOVE;
        return -1;
    }
    sup->modules[slot].pending_switch = 1u;
    sup_refresh_flags(sup, (uint32_t)slot);
    return 1;
}

uint32_t canopus_supervisor_scan_packages(struct canopus_supervisor_v1 *sup)
{
    const struct canopus_sup_platform_v1 *pf;
    uint32_t scanned = 0u, added = 0u, updated = 0u, failed = 0u;
    uint32_t index;
    uint32_t rc;
    uint32_t last_error = CANOPUS_SUP_ERR_NONE;
    uint32_t changed_slots = 0u;
    char ids[CANOPUS_SUP_MODULE_SLOTS][CANOPUS_SUP_MODULE_ID_MAX];
    uint32_t count = 0u;

    if (sup == 0 || sup->platform == 0 || canopus_supervisor_bus_dispatching()) return CANOPUS_RESULT_FAILED;
    pf = sup->platform;
    if (pf->list_packages == 0) {
        sup->error_code = CANOPUS_SUP_ERR_STAGE;
        return CANOPUS_RESULT_FAILED;
    }
    while (count < CANOPUS_SUP_MODULE_SLOTS) {
        int listed = pf->list_packages(sup->platform_cookie, count, ids[count]);
        if (listed < 0) { sup->error_code = CANOPUS_SUP_ERR_STAGE; return CANOPUS_RESULT_FAILED; }
        if (listed != 0) break;
        count++;
    }
    for (index = 0u; index < count; index++) {
        const char *id = ids[index];
        int previous_slot = sup_find_slot_by_id(sup, id);
        uint32_t previous_flags = previous_slot >= 0
                                      ? sup->modules[previous_slot].flags : 0u;
        int staged;
        scanned++;
        sup->error_code = CANOPUS_SUP_ERR_NONE;
        staged = canopus_supervisor_stage_token(sup, id);
        if (staged == 0 || staged == 1) {
            int changed_slot = sup_find_slot_by_id(sup, id);
            if (changed_slot >= 0 && (staged == 0 ||
                sup->modules[changed_slot].flags != previous_flags)) {
                changed_slots |= 1u << (uint32_t)changed_slot;
            }
        }
        if (staged == 0) {
            added++;
        } else if (staged == 1) {
            updated++;
        } else if ((int32_t)sup->error_code != CANOPUS_SUP_ERR_STAGE_DUPLICATE &&
                   (int32_t)sup->error_code != CANOPUS_SUP_ERR_STAGE_DOWNGRADE) {
            failed++;
            last_error = sup->error_code;
        }
    }
    sup->error_code = last_error;
#define SAT(v) ((v) > 255u ? 255u : (v))
    sup->scan_summary = SAT(added) | (SAT(updated) << 8) | (SAT(failed) << 16) |
                        (SAT(scanned) << 24);
#undef SAT
    rc = failed != 0u && added == 0u && updated == 0u ? CANOPUS_RESULT_FAILED
         : updated != 0u ? CANOPUS_RESULT_REBOOT_REQUIRED
                         : CANOPUS_RESULT_COMPLETED;
    if (added != 0u || updated != 0u) {
        int persist_rc = canopus_supervisor_save_registry(sup);
        if (persist_rc != 0) {
            sup->error_code = (uint32_t)persist_rc;
            rc = CANOPUS_RESULT_FAILED;
        }
    }
    for (index = 0u; index < CANOPUS_SUP_MODULE_SLOTS; index++) {
        if ((changed_slots & (1u << index)) != 0u) canopus_supervisor_module_changed(sup, index);
    }
    canopus_supervisor_module_events_flush(sup);
    return rc;
}

/* ---- CAN-P0-005 revision: next-boot registry persistence ------------ */

int canopus_supervisor_serialize_registry(struct canopus_supervisor_v1 *sup,
                                          uint8_t *buf, uint32_t len)
{
    uint32_t i;
    if (sup == 0 || buf == 0 || len < CANOPUS_SUP_REGISTRY_SIZE) {
        return -1;
    }
    canopus_memset(buf, 0, CANOPUS_SUP_REGISTRY_SIZE);
    put_wire_u32(buf, 0u, CANOPUS_SUP_REGISTRY_MAGIC);
    put_wire_u32(buf, 4u, CANOPUS_SUP_REGISTRY_VERSION);
    put_wire_u32(buf, 8u, sup->module_count);
    put_wire_u32(buf, 12u, sup_boot_word(sup));
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        const struct canopus_sup_module_v1 *m = &sup->modules[i];
        uint32_t o = CANOPUS_SUP_REGISTRY_HEADER +
                     i * CANOPUS_SUP_REGISTRY_SLOT_SIZE;
        if (m->state != 0u) {
            canopus_memcpy(buf + o, m->module_id, CANOPUS_SUP_MODULE_ID_MAX);
            put_wire_u32(buf, o + 32u, m->lifecycle_class);
            put_wire_u32(buf, o + 36u, m->version);
            put_wire_u32(buf, o + 40u,
                         (m->active_gen ? CANOPUS_SUP_REG_FLAG_ACTIVE_B : 0u) |
                         (m->pending_switch ? CANOPUS_SUP_REG_FLAG_PENDING : 0u) |
                         (sup_registry_error_encode(m->activation_error) <<
                          CANOPUS_SUP_REGISTRY_ERROR_SHIFT));
            put_wire_u32(buf, o + 44u, m->intent | ((uint32_t)m->starts << 8) |
                         ((uint32_t)m->native_app_id << 16));
        }
    }
    return 0;
}

int canopus_supervisor_save_registry(struct canopus_supervisor_v1 *sup)
{
    uint8_t buf[CANOPUS_SUP_REGISTRY_SIZE];
    int rc;
    if (sup == 0 || sup->platform == 0 || sup->platform->persist == 0) {
        return -1;
    }
    rc = canopus_supervisor_serialize_registry(sup, buf, sizeof(buf));
    if (rc != 0) return rc;
    rc = sup->platform->persist(sup->platform_cookie, buf, sizeof(buf));
    if (rc == 0) sup->boot_saved = wire_u32(buf + 12u);
    return rc;
}

/* Restore the slot table after a fresh load. Absent registry == fresh
 * install (not an error). Enabled intents are loaded through the platform;
 * remove intents delete their artifacts and never re-register. */
static int sup_restore_registry(struct canopus_supervisor_v1 *sup,
                                int load_enabled_modules)
{
    /* The boot constructor runs on the firmware exec task's stack (3.1.28):
     * a 784 B frame here plus the platform path buffers overflows it and the
     * next screen sleep resets the SoC. The restore is a single-threaded boot
     * step, so the buffer lives with the Supervisor image. */
    static uint8_t buf[CANOPUS_SUP_REGISTRY_SIZE];
    uint32_t i, native_ids = 0u;
    int rc;
    if (sup == 0 || sup->platform == 0 || sup->platform->restore == 0) {
        return -1;
    }
    rc = sup->platform->restore(sup->platform_cookie, buf, sizeof(buf));
    if (rc != 0) {
        return rc > 0 ? 0 : -1; /* absent (1) is fine; hard error fails */
    }
    if (wire_u32(buf) != CANOPUS_SUP_REGISTRY_MAGIC) {
        /* Not a Canopus registry at all: nothing in it can be trusted to load. */
        sup_enter_safe_mode(sup, CANOPUS_SAFE_MODE_STORE_CORRUPT);
        return -1;
    }
    if (wire_u32(buf + 4u) != CANOPUS_SUP_REGISTRY_VERSION) {
        return -1; /* an older format is ignored, and the user told to reinstall (K11) */
    }
    sup_boot_check(sup, wire_u32(buf + 12u));
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        uint32_t o = CANOPUS_SUP_REGISTRY_HEADER +
                     i * CANOPUS_SUP_REGISTRY_SLOT_SIZE;
        const uint8_t *id = buf + o;
        uint32_t lifecycle_class = wire_u32(buf + o + 32u);
        uint32_t version = wire_u32(buf + o + 36u);
        uint32_t flags = wire_u32(buf + o + 40u);
        uint32_t intent_word = wire_u32(buf + o + 44u);
        uint32_t intent = intent_word & 0xffu;
        int slot;

        if (id[0] == 0u) {
            continue; /* empty entry */
        }
        if (lifecycle_class > CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED) {
            continue;
        }
        if (intent == CANOPUS_SUP_INTENT_REMOVE) {
            /* commit the removal: register briefly so the platform hook can
             * resolve the module id, delete the artifacts, then reclaim */
            slot = canopus_supervisor_add_module(
                sup, lifecycle_class, version, (const char *)id);
            if (slot >= 0) {
                canopus_supervisor_release_services(sup, (uint32_t)slot);
                if (sup->platform->remove_artifact != 0) {
                    (void)sup->platform->remove_artifact(
                        sup->platform_cookie, (uint32_t)slot);
                }
                sup_clear_slot(sup, slot);
            }
            continue;
        }
        slot = canopus_supervisor_add_module(
            sup, lifecycle_class, version, (const char *)id);
        if (slot < 0) {
            continue;
        }
        sup->modules[slot].intent =
            intent == CANOPUS_SUP_INTENT_ENABLED
                ? CANOPUS_SUP_INTENT_ENABLED : CANOPUS_SUP_INTENT_DISABLED;
        sup->modules[slot].activation_error =
            sup_registry_error_decode(flags);
        sup->modules[slot].starts = (uint8_t)(intent_word >> 8);
        sup->modules[slot].native_app_id = (uint8_t)(intent_word >> 16);
        if (sup->modules[slot].native_app_id != 0u) {
            uint32_t index = (uint32_t)sup->modules[slot].native_app_id - CANOPUS_SUP_NATIVE_APP_FIRST_ID;
            if (index >= CANOPUS_SUP_NATIVE_APP_IDS || (native_ids & (1u << index)) != 0u) {
                sup_enter_safe_mode(sup, CANOPUS_SAFE_MODE_STORE_CORRUPT);
                return -1;
            }
            native_ids |= 1u << index;
        }
        sup->modules[slot].active_gen =
            (flags & CANOPUS_SUP_REG_FLAG_ACTIVE_B) != 0u;
        sup->modules[slot].pending_switch =
            (flags & CANOPUS_SUP_REG_FLAG_PENDING) != 0u;
        /* Full restore is deliberately two-phase: import every slot before
         * loading code, so an intermediate save can never truncate trailing
         * metadata. The production constructor uses the metadata-only phase
         * for the same low-stack reason. */
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        sup_refresh_flags(sup, i);
    }
    if (load_enabled_modules && sup->platform->load_module != 0) {
        (void)canopus_supervisor_activate_restored_modules(sup);
    }
    return 0;
}

int canopus_supervisor_restore_registry(struct canopus_supervisor_v1 *sup)
{
    return sup_restore_registry(sup, 1);
}

int canopus_supervisor_restore_registry_metadata(struct canopus_supervisor_v1 *sup)
{
    return sup_restore_registry(sup, 0);
}

int canopus_supervisor_activate_restored_modules(struct canopus_supervisor_v1 *sup)
{
    uint32_t i;
    int changed;
    int failed = 0;

    if (sup == 0 || sup->platform == 0 || sup->platform->load_module == 0 ||
        canopus_supervisor_bus_dispatching()) {
        return -1;
    }
    if (sup->safe_mode) {
        sup->boot_restored = 1u;
        canopus_supervisor_boot_notify(sup);
        return 0; /* nothing third-party runs in safe mode */
    }
    changed = sup_apply_pending_switches(sup);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].intent == CANOPUS_SUP_INTENT_ENABLED &&
            sup->modules[i].state == CANOPUS_STATE_INSTALLED) {
            sup->boot_state = CANOPUS_BOOT_BOOTING; /* persisted by the first start */
            break;
        }
    }
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        struct canopus_sup_module_v1 *module = &sup->modules[i];

        /* The caller must own the UI transaction. Rebinding a resident
         * module is separate from loading a new image, and does not establish
         * that a task restart reset the firmware's graphics state. */
        if (module->intent == CANOPUS_SUP_INTENT_ENABLED &&
            module->state == CANOPUS_STATE_BOOT_RESIDENT &&
            module->descriptor != 0 &&
            (module->descriptor->flags &
             CANOPUS_FLAG_REACTIVATE_AFTER_UI_RESTART) != 0u) {
            int rebind_rc = module->descriptor->activate != 0
                ? module->descriptor->activate(canopus_supervisor_context(sup, i))
                : CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
            module->activation_error = (uint32_t)rebind_rc;
            if (rebind_rc != 0) {
                failed = -1;
                sup->error_code = CANOPUS_SUP_ERR_ACTIVATE;
            }
            canopus_supervisor_module_changed(sup, i);
            canopus_supervisor_module_events_flush(sup);
            continue;
        }
        if (module->intent != CANOPUS_SUP_INTENT_ENABLED ||
            module->state != CANOPUS_STATE_INSTALLED) {
            continue;
        }
        changed = 1;
        if (sup_start_module(sup, i) < 0) {
            failed = -1;
        }
        canopus_supervisor_module_changed(sup, i);
        canopus_supervisor_module_events_flush(sup);
    }
    if (sup->unstable_modules != 0u && canopus_supervisor_watch_stability(sup) != 0) {
        for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
            sup_mark_stable(sup, &sup->modules[i]);
        }
    }
    if (sup->unstable_modules == 0u &&
        (sup->boot_state == CANOPUS_BOOT_BOOTING || sup->boot_trial)) {
        sup_boot_commit_ok(sup); /* nothing left to prove this boot */
    }
    sup->boot_restored = 1u;
    canopus_supervisor_boot_notify(sup);
    if ((changed || sup_boot_word(sup) != sup->boot_saved) &&
        canopus_supervisor_save_registry(sup) != 0) {
        canopus_supervisor_module_events_flush(sup);
        return -1;
    }
    canopus_supervisor_module_events_flush(sup);
    return failed;
}
