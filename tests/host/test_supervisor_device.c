/* Host tests: device-side supervisor status/command ABI (Phase 5).
 * Drives canopus_supervisor.c through a fake platform and checks the exact
 * 384-byte status + 16-byte command ABI the Lua installer watchface uses. */
#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <string.h>

/* ---- fake platform ------------------------------------------------- */

static int g_loads;
static int g_artifact_removals;
static int g_stages;
static int g_persists;
static int g_load_result = CANOPUS_STATE_ACTIVE;
static int g_stage_result = 0;
static uint8_t g_registry[CANOPUS_SUP_REGISTRY_SIZE];
static int g_registry_present;

static int fake_register(void *c) { (void)c; return 0; }
static int fake_unregister(void *c) { (void)c; return 0; }
static int fake_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    (void)c; (void)i; (void)n; (void)cls;
    g_loads++;
    return g_load_result;
}
static int fake_remove_artifact(void *c, uint32_t i)
{
    (void)c; (void)i; g_artifact_removals++; return 0;
}
static int fake_stage(void *c, const char *p)
{
    (void)c; (void)p; g_stages++; return g_stage_result;
}
static int fake_persist(void *c, const uint8_t *d, uint32_t n)
{
    (void)c;
    if (d == 0 || n == 0u || n > sizeof(g_registry)) {
        return -1;
    }
    canopus_memcpy(g_registry, d, n);
    g_persists++;
    g_registry_present = 1;
    return 0;
}
static int fake_restore(void *c, uint8_t *d, uint32_t n)
{
    (void)c;
    if (d == 0 || n == 0u || n > sizeof(g_registry)) {
        return -1;
    }
    if (!g_registry_present) {
        return 1; /* no registry yet: a fresh install */
    }
    canopus_memcpy(d, g_registry, n);
    return 0;
}

static const struct canopus_sup_platform_v1 fake_platform = {
    fake_register, fake_unregister, fake_load, fake_stage,
    fake_remove_artifact, fake_persist, fake_restore,
};

/* ---- command/status builders --------------------------------------- */

static uint32_t r32(const uint8_t *b, unsigned int o)
{
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
           ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

static void make_command(uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE],
                         uint32_t magic, uint32_t op, uint32_t a0, uint32_t a1)
{
    unsigned int i;
    canopus_memset(cmd, 0, CANOPUS_SUP_COMMAND_SIZE);
    cmd[0] = (uint8_t)(magic & 0xff);
    cmd[1] = (uint8_t)((magic >> 8) & 0xff);
    cmd[2] = (uint8_t)((magic >> 16) & 0xff);
    cmd[3] = (uint8_t)((magic >> 24) & 0xff);
    for (i = 0; i < 3; i++) {
        uint32_t v = i == 0 ? op : (i == 1 ? a0 : a1);
        cmd[4 + i * 4] = (uint8_t)(v & 0xff);
        cmd[5 + i * 4] = (uint8_t)((v >> 8) & 0xff);
        cmd[6 + i * 4] = (uint8_t)((v >> 16) & 0xff);
        cmd[7 + i * 4] = (uint8_t)((v >> 24) & 0xff);
    }
}

/* ---- ABI shape (must match main.lua) -------------------------------- */

TEST(supervisor_init_rejects_null)
{
    CHECK(canopus_supervisor_init(0, 7, &fake_platform, 0) == -1);
}

TEST(supervisor_status_abi_layout)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_render_status(&sup, status) == 0);
    /* offsets used by main.lua */
    CHECK(r32(status, 0) == CANOPUS_SUP_STATUS_MAGIC);
    CHECK(r32(status, 4) == CANOPUS_SUP_ABI);
    CHECK(r32(status, 8) == 7);   /* framework_revision */
    CHECK(r32(status, 12) == 0);  /* safe_mode */
    CHECK(r32(status, 16) == 0);  /* module_count */
    CHECK(r32(status, 20) == 0);  /* pending_op */
    CHECK(r32(status, 24) == 0);  /* pending_state */
    /* module slots start at 128, 16-byte stride */
    CHECK(r32(status, 128 + 0 * 16 + 0) == 0);
    CHECK(r32(status, 128 + 15 * 16 + 12) == 0);
}

TEST(supervisor_command_abi_validates)
{
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_validate_command(cmd) == 0);
    make_command(cmd, 0xDEADBEEF, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_validate_command(cmd) == -1);
}

TEST(supervisor_device_transfers_report_bytes)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);

    g_stages = 0;
    CHECK(canopus_supervisor_device_write(&sup, cmd, sizeof(cmd)) ==
          CANOPUS_SUP_COMMAND_SIZE);
    CHECK(g_stages == 1);
    CHECK(sup.pending_state == CANOPUS_RESULT_COMPLETED);
    CHECK(canopus_supervisor_device_write(&sup, cmd, sizeof(cmd) - 1) == -1);
    CHECK(canopus_supervisor_device_write(&sup, cmd, sizeof(cmd) + 1) == -1);

    CHECK(canopus_supervisor_device_read(&sup, status, sizeof(status)) ==
          CANOPUS_SUP_STATUS_SIZE);
    CHECK(r32(status, 24) == CANOPUS_RESULT_COMPLETED);
    CHECK(canopus_supervisor_device_read(&sup, status, sizeof(status) - 1) == -1);
}

TEST(supervisor_query_returns_completed)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(sup.pending_op == CANOPUS_SUP_CMD_QUERY);
    CHECK(sup.pending_state == CANOPUS_RESULT_COMPLETED);
    canopus_supervisor_render_status(&sup, status);
    CHECK(r32(status, 20) == CANOPUS_SUP_CMD_QUERY);
    CHECK(r32(status, 24) == CANOPUS_RESULT_COMPLETED);
}

TEST(supervisor_install_stages_package)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_stages = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_stages == 1);
}

/* ---- CAN-P0-005 revision: next-boot lifecycle ----------------------- */

TEST(supervisor_enable_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    CHECK(sup.modules[0].state == CANOPUS_STATE_INSTALLED);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    g_loads = 0;
    g_persists = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(g_loads == 0); /* nothing loads at ENABLE time */
    CHECK(sup.modules[0].state == CANOPUS_STATE_ENABLED);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_ENABLED);
    CHECK(g_persists == 1); /* the boot intent was persisted */
}

TEST(supervisor_disable_loaded_module_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    g_persists = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    /* a loaded module stays resident; only the intent changes */
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED_NEXT_BOOT);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    CHECK(g_persists == 1);
}

TEST(supervisor_disable_installed_module_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED);
}

TEST(supervisor_remove_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    g_artifact_removals = 0;
    g_persists = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    /* the module stays resident; artifact deletion happens at the next boot */
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_REMOVE);
    CHECK(g_artifact_removals == 0);
    CHECK(g_persists == 1);
}

TEST(supervisor_remove_pending_slot_not_reclaimed_by_command)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.one") == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.two") == 1);
    CHECK(sup.module_count == 2);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 1, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.module_count == 2); /* pending removal does not reclaim */
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
    CHECK(sup.modules[1].state == CANOPUS_STATE_REMOVE_PENDING);
}

/* ---- CAN-P0-005 revision: registry persistence ---------------------- */

TEST(supervisor_install_persists_disabled_module)
{
    struct canopus_supervisor_v1 a, b;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    g_registry_present = 0;
    g_persists = 0;
    /* a freshly staged, disabled module sits in the table when INSTALL
     * completes (stage_package registers it before returning COMPLETED) */
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    CHECK(a.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);
    CHECK(canopus_supervisor_handle_command(&a, cmd) == CANOPUS_RESULT_COMPLETED);
    /* §16.4: INSTALL persists the table, so an installed-but-never-enabled
     * module survives a reboot. */
    CHECK(g_persists == 1);
    CHECK(g_registry_present == 1);
    /* a fresh supervisor restores it as disabled, not lost */
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(b.module_count == 1);
    CHECK(b.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    CHECK(g_loads == 0); /* disabled intents never load */
}

TEST(supervisor_registry_survives_reload)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_REMOVABLE, 3, 1, "mod.hello") == 0);
    a.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    a.modules[0].state = CANOPUS_STATE_ENABLED;
    g_persists = 0;
    CHECK(canopus_supervisor_save_registry(&a) == 0);
    CHECK(g_persists == 1);
    /* a fresh supervisor (the reload after reboot / reinstall) restores the
     * slot and loads the enabled module */
    g_loads = 0;
    g_load_result = CANOPUS_STATE_ACTIVE;
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(b.module_count == 1);
    CHECK(b.modules[0].intent == CANOPUS_SUP_INTENT_ENABLED);
    CHECK(b.modules[0].state == CANOPUS_STATE_ACTIVE);
    CHECK(g_loads == 1);
}

TEST(supervisor_registry_restore_keeps_disabled_modules_unloaded)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1, "mod.bt") == 0);
    CHECK(a.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    CHECK(canopus_supervisor_save_registry(&a) == 0);
    g_loads = 0;
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(b.module_count == 1);
    CHECK(b.modules[0].state == CANOPUS_STATE_INSTALLED); /* not loaded */
    CHECK(g_loads == 0);
}

TEST(supervisor_registry_remove_intent_clears_at_boot)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1, "mod.bt") == 0);
    a.modules[0].intent = CANOPUS_SUP_INTENT_REMOVE;
    a.modules[0].state = CANOPUS_STATE_REMOVE_PENDING;
    CHECK(canopus_supervisor_save_registry(&a) == 0);
    g_artifact_removals = 0;
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(g_artifact_removals == 1); /* inbox artifacts deleted */
    CHECK(b.module_count == 0);      /* slot not re-registered */
}

TEST(supervisor_registry_corrupt_magic_is_ignored)
{
    struct canopus_supervisor_v1 b;
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 1;
    canopus_memset(g_registry, 0, sizeof(g_registry));
    g_registry[0] = 0xEE; /* bad magic */
    CHECK(canopus_supervisor_restore_registry(&b) == -1);
    CHECK(b.module_count == 0);
    g_registry_present = 0;
}

TEST(supervisor_resident_disable_is_reboot_required)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1, "mod.bt") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED_NEXT_BOOT);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
}

TEST(supervisor_resident_remove_is_remove_pending)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1, "mod.bt") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_REMOVE);
}

TEST(supervisor_unknown_slot_disallowed)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 99, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 3, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
}

TEST(supervisor_safe_mode_sets_flag)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(sup.safe_mode == 1);
    CHECK(sup.safe_mode_reason == CANOPUS_SAFE_MODE_USER_REQUESTED);
    canopus_supervisor_render_status(&sup, status);
    CHECK(r32(status, 12) == 1);
}

/* ---- CAN-P0-006: safe-mode policy matrix + boot markers ------------- */

TEST(supervisor_safe_mode_rejects_activation)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1,
                                        "mod.hello") == 0);
    /* enter safe mode */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    /* INSTALL / ENABLE / UPDATE are rejected by policy, not just shown */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_SAFE_MODE);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_UPDATE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    /* QUERY stays read-only and allowed */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
}

TEST(supervisor_safe_mode_allows_next_boot_disable_remove)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1,
                                        "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    /* next-boot disable/remove never run third-party code, so safe mode
     * allows them for every lifecycle class */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED_NEXT_BOOT);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
}

TEST(supervisor_safe_mode_allows_resident_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1,
                                        "mod.bt") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    /* disable-next-boot for a resident module is read-only and allowed */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED_NEXT_BOOT);
}

TEST(supervisor_crash_counter_saturates)
{
    struct canopus_supervisor_v1 sup;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    sup.crash_counter = UINT32_MAX;
    canopus_supervisor_record_crash(&sup);
    CHECK_EQ(sup.crash_counter, UINT32_MAX); /* no wrap */
}

TEST(supervisor_render_status_rejects_null)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_render_status(0, status) == -1);
    CHECK(canopus_supervisor_render_status(&sup, 0) == -1);
    CHECK(canopus_supervisor_render_status(&sup, status) == 0);
}

TEST(supervisor_boot_markers_drive_safe_mode)
{
    struct canopus_supervisor_v1 sup;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    /* a boot that never commits BOOT_OK forces safe mode next boot */
    canopus_supervisor_boot_begin(&sup, 42);
    CHECK(sup.boot_state == CANOPUS_BOOT_BOOTING);
    CHECK(canopus_supervisor_boot_should_safe_mode(&sup) != 0);
    canopus_supervisor_boot_ok(&sup);
    CHECK(sup.boot_state == CANOPUS_BOOT_OK);
    CHECK(canopus_supervisor_boot_should_safe_mode(&sup) == 0);
    /* a crash counter past the threshold forces safe mode */
    sup.crash_counter = CANOPUS_SUP_CRASH_THRESHOLD;
    CHECK(canopus_supervisor_boot_should_safe_mode(&sup) != 0);
}

/* ---- CAN-P1-003: sequence snapshot ---------------------------------- */

TEST(supervisor_status_sequence_embedded_and_even)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_render_status(&sup, status) == 0);
    /* begin == end, both even (init leaves the snapshot ready) */
    CHECK_EQ(r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF),
             r32(status, CANOPUS_SUP_STATUS_SEQ_END_OFF));
    CHECK((r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF) & 1u) == 0u);
    CHECK_EQ(r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF), 0u);
}

TEST(supervisor_command_advances_sequence)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    uint32_t seq;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    canopus_supervisor_render_status(&sup, status);
    seq = r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF);
    CHECK_EQ(seq, r32(status, CANOPUS_SUP_STATUS_SEQ_END_OFF));
    CHECK_EQ(seq, 2u); /* init(0) -> begin(1) -> commit(2) */
    /* a second command advances it further */
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    canopus_supervisor_render_status(&sup, status);
    CHECK_EQ(r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF), 4u);
}

TEST(supervisor_read_rejects_torn_snapshot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    /* simulate a mid-write (odd sequence): the reader must not publish it */
    sup.snap.sequence = sup.snap.sequence | 1u;
    CHECK(canopus_supervisor_device_read(&sup, status, sizeof(status)) == -1);
    /* after the mutation commits, the same read succeeds and is consistent */
    canopus_snapshot_commit(&sup.snap);
    CHECK(canopus_supervisor_device_read(&sup, status, sizeof(status)) ==
          CANOPUS_SUP_STATUS_SIZE);
    CHECK_EQ(r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF),
             r32(status, CANOPUS_SUP_STATUS_SEQ_END_OFF));
    CHECK((r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF) & 1u) == 0u);
}

TEST(supervisor_read_staging_never_torn)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    /* the read returns a fully consistent record: slot state present, no
     * torn mix of module states */
    CHECK(canopus_supervisor_device_read(&sup, status, sizeof(status)) ==
          CANOPUS_SUP_STATUS_SIZE);
    CHECK_EQ(r32(status, 128 + 0 * CANOPUS_SUP_MODULE_SLOT_STRIDE + 0),
             CANOPUS_STATE_ENABLED);
    CHECK_EQ(r32(status, CANOPUS_SUP_STATUS_SEQ_BEGIN_OFF),
             r32(status, CANOPUS_SUP_STATUS_SEQ_END_OFF));
}

/* ---- CAN-P0-008: v2 transport -------------------------------------- */

static void make_v2_request(uint8_t *buf, uint32_t cap, uint32_t opcode,
                            uint32_t request_id, const void *payload,
                            uint32_t payload_len)
{
    uint32_t total;
    canopus_memset(buf, 0, cap);
    total = CANOPUS_TRANSPORT_V2_HEADER_SIZE + payload_len;
    buf[0] = 0x32; buf[1] = 0x43; buf[2] = 0x50; buf[3] = 0x43; /* "CPC2" */
    buf[4] = (uint8_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE & 0xff);
    buf[5] = (uint8_t)((CANOPUS_TRANSPORT_V2_HEADER_SIZE >> 8) & 0xff);
    buf[6] = (uint8_t)CANOPUS_TRANSPORT_V2_REQUEST;
    buf[8] = (uint8_t)(CANOPUS_ABI_MAJOR & 0xff);
    buf[9] = (uint8_t)((CANOPUS_ABI_MAJOR >> 8) & 0xff);
    buf[10] = (uint8_t)(CANOPUS_ABI_MINOR & 0xff);
    buf[11] = (uint8_t)((CANOPUS_ABI_MINOR >> 8) & 0xff);
#define PV2(o, v) \
    do { uint32_t _v = (uint32_t)(v); \
         buf[(o)] = (uint8_t)(_v & 0xff); buf[(o) + 1] = (uint8_t)((_v >> 8) & 0xff); \
         buf[(o) + 2] = (uint8_t)((_v >> 16) & 0xff); buf[(o) + 3] = (uint8_t)((_v >> 24) & 0xff); } while (0)
    PV2(12, total);
    PV2(16, opcode);
    PV2(20, request_id);
    PV2(24, 0); /* flags */
    PV2(28, 0); /* result */
    PV2(32, payload_len);
#undef PV2
    if (payload_len > 0 && payload != 0) {
        canopus_memcpy(buf + CANOPUS_TRANSPORT_V2_HEADER_SIZE, payload, payload_len);
    }
}

static uint32_t v2_word(const uint8_t *b, uint32_t o)
{
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
           ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

TEST(v2_golden_query_device_request)
{
    const uint8_t golden[36] = {
        0x32,0x43,0x50,0x43, 0x24,0x00, 0x01,0x00, 0x01,0x00, 0x00,0x00,
        0x24,0x00,0x00,0x00, 0x09,0x00,0x00,0x00, 0x34,0x12,0x00,0x00,
        0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00, 0x00,0x00,0x00,0x00,
    };
    struct canopus_proto_request_v1 req;
    uint32_t poff = 0;
    CHECK(canopus_transport_v2_decode_request(golden, sizeof(golden), &req, &poff) == 0);
    CHECK_EQ(req.magic, CANOPUS_TRANSPORT_V2_MAGIC);
    CHECK_EQ(req.command, CANOPUS_CMD_QUERY_DEVICE);
    CHECK_EQ(req.request_id, 0x1234u);
    CHECK_EQ(req.payload_size, 0u);
    CHECK_EQ(poff, 36u);
}

TEST(v2_decode_rejects_malformed)
{
    uint8_t buf[CANOPUS_TRANSPORT_V2_HEADER_SIZE + 8];
    struct canopus_proto_request_v1 req;
    uint32_t poff = 0;
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == 0);

    buf[0] = 0xFF; /* bad magic */
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);

    buf[6] = CANOPUS_TRANSPORT_V2_RESPONSE; /* wrong kind */
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);

    buf[8] = 99; /* wrong ABI major */
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);

    buf[10] = 5; /* newer minor fails closed */
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);

    /* request id 0 is reserved */
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 0, 0, 0);
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);

    /* buffer shorter than the declared record */
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);
    CHECK(canopus_transport_v2_decode_request(buf, 20, &req, &poff) == -1);

    /* CAN-P2-003: an unknown flag bit fails closed */
    make_v2_request(buf, sizeof(buf), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);
    buf[24] = 0x08; /* flags @24: set an unknown bit */
    CHECK(canopus_transport_v2_decode_request(buf, sizeof(buf), &req, &poff) == -1);
}

TEST(v2_device_write_echoes_request_id)
{
    struct canopus_supervisor_v1 sup;
    uint8_t wbuf[CANOPUS_TRANSPORT_V2_HEADER_SIZE];
    uint8_t rbuf[128];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_QUERY_DEVICE, 0x1234, 0, 0);
    CHECK(canopus_supervisor_device_write(&sup, wbuf, sizeof(wbuf)) ==
          (int32_t)sizeof(wbuf));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) ==
          CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_QUERY_DEVICE_SIZE);
    CHECK(v2_word(rbuf, 12) ==
          CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_QUERY_DEVICE_SIZE);
    CHECK(v2_word(rbuf, 32) == CANOPUS_QUERY_DEVICE_SIZE);
    CHECK(v2_word(rbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE) ==
          CANOPUS_QUERY_DEVICE_MAGIC);
    CHECK(v2_word(rbuf, 0) == CANOPUS_TRANSPORT_V2_MAGIC);
    CHECK((v2_word(rbuf, 6) & 0xffffu) == CANOPUS_TRANSPORT_V2_RESPONSE);
    CHECK(v2_word(rbuf, 16) == CANOPUS_CMD_QUERY_DEVICE); /* opcode echoed */
    CHECK(v2_word(rbuf, 20) == 0x1234); /* request id echoed */
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_COMPLETED);
}

TEST(v2_request_tracked_in_pending)
{
    struct canopus_supervisor_v1 sup;
    uint8_t wbuf[96];
    uint8_t rbuf[128];
    uint8_t payload[24];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    /* INSTALL requires a bounded stage token (CAN-P0-003) */
    canopus_memset(payload, 0, sizeof(payload));
    canopus_memcpy(payload, "org.example.pkg", 15);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x77,
                    payload, 16);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 16) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 16));
    const struct canopus_pending_request_v1 *p =
        canopus_pending_find(&sup.pending, 0x77);
    CHECK(p != 0);
    CHECK(p->command == CANOPUS_CMD_INSTALL);
    CHECK(p->state == CANOPUS_RESULT_COMPLETED); /* retained terminal */
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) ==
          CANOPUS_TRANSPORT_V2_HEADER_SIZE);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_COMPLETED);
}

TEST(v2_enable_by_module_id)
{
    struct canopus_supervisor_v1 sup;
    uint8_t wbuf[128];
    uint8_t rbuf[128];
    uint8_t payload[CANOPUS_SUP_MODULE_ID_MAX];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1,
                                        "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_INSTALLED;

    canopus_memset(payload, 0, sizeof(payload));
    canopus_memcpy(payload, "mod.hello", 9);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_ENABLE, 0x99,
                    payload, sizeof(payload));
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    /* enable is next-boot and never loads now */
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_ENABLED);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_ENABLED);

    /* an unknown module id resolves to DISALLOWED, not a fake success */
    canopus_memcpy(payload, "mod.nope", 8);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_ENABLE, 0x9A,
                    payload, sizeof(payload));
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);
}

/* ---- CAN-P0-003: INSTALL stage token boundary ------------------------ */

TEST(v2_install_accepts_only_bounded_token)
{
    struct canopus_supervisor_v1 sup;
    uint8_t wbuf[128];
    uint8_t rbuf[128];
    uint8_t payload[64];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_stage_result = 0;

    /* a valid package token is accepted and staged */
    canopus_memset(payload, 0, sizeof(payload));
    canopus_memcpy(payload, "org.example.pkg-1.0", 19);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x10,
                    payload, 20);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 20) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 20));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_COMPLETED);

    /* a path / traversal is rejected, never passed to the platform */
    canopus_memcpy(payload, "/etc/passwd", 11);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x11,
                    payload, 12);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 12) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 12));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);

    canopus_memcpy(payload, "../evil.bin", 11);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x12,
                    payload, 12);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 12) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 12));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);

    /* "." and ".." are traversal components, not basenames */
    canopus_memcpy(payload, ".", 2);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x13,
                    payload, 2);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 2) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 2));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);

    canopus_memcpy(payload, "..", 3);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x14,
                    payload, 3);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 3) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 3));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);

    /* a leading dot (hidden name) is also rejected */
    canopus_memcpy(payload, ".hidden", 8);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x15,
                    payload, 8);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 8) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 8));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_DISALLOWED);
}

TEST(v2_unknown_magic_rejected)
{
    struct canopus_supervisor_v1 sup;
    uint8_t bad[64];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    canopus_memset(bad, 0xEE, sizeof(bad));
    CHECK(canopus_supervisor_device_write(&sup, bad, CANOPUS_SUP_COMMAND_SIZE) == -1);
    CHECK(canopus_supervisor_device_write(&sup, bad, sizeof(bad)) == -1);
    /* a truncated v2 frame (magic ok, record too short) is rejected */
    make_v2_request(bad, sizeof(bad), CANOPUS_CMD_QUERY_DEVICE, 1, 0, 0);
    CHECK(canopus_supervisor_device_write(&sup, bad, 20) == -1);
}

/* ---- CAN-P1-008: error code persistence semantics ------------------ */

TEST(supervisor_error_persists_until_next_command)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);

    /* a failing INSTALL leaves a readable, stable error */
    g_stage_result = -1;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_FAILED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_STAGE);

    /* a later successful QUERY clears it — the next success does not
     * inherit the previous failure */
    g_stage_result = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_NONE);
}

TEST(supervisor_bad_slot_sets_error)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 99, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_BAD_SLOT);
}

TEST(supervisor_enable_never_loads_so_no_load_error)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_INSTALLED;
    /* even a platform whose load would fail is never touched: enable only
     * records the boot intent */
    g_load_result = -1;
    g_loads = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(g_loads == 0);
    CHECK(sup.modules[0].state == CANOPUS_STATE_ENABLED);
}

TEST(supervisor_unknown_op_sets_error)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, 0xDEAD0000u, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REJECTED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_UNKNOWN_OP);
}

/* ---- CAN-P1-007: slot reclaim + authoritative identity -------------- */

TEST(supervisor_status_reflects_remove_pending)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.two") == 1);
    CHECK(sup.module_count == 2);
    /* remove slot 0: the enumerated count and slot stay consistent; the
     * pending removal is visible in the ABI */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.module_count == 2); /* pending removal does not reclaim */
    canopus_supervisor_render_status(&sup, status);
    CHECK_EQ(r32(status, 16), 2u);
    CHECK_EQ(r32(status, 128 + 0 * CANOPUS_SUP_MODULE_SLOT_STRIDE + 0),
             CANOPUS_STATE_REMOVE_PENDING);
    CHECK(r32(status, 128 + 1 * CANOPUS_SUP_MODULE_SLOT_STRIDE + 0) != 0u);
}

TEST(supervisor_add_module_rejects_full_table)
{
    struct canopus_supervisor_v1 sup;
    uint32_t i;
    int added = 0;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS + 2; i++) {
        if (canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1, "mod.hello") >= 0) {
            added++;
        }
    }
    CHECK(added == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(sup.module_count == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(canopus_supervisor_add_module(&sup, 99, 1, 1, "mod.bad") == -1); /* bad class */
}

static const struct test_registry supervisor_device_tests[] = {
    { "supervisor_init_rejects_null", supervisor_init_rejects_null_wrapper },
    { "supervisor_status_abi_layout", supervisor_status_abi_layout_wrapper },
    { "supervisor_command_abi_validates", supervisor_command_abi_validates_wrapper },
    { "supervisor_device_transfers_report_bytes", supervisor_device_transfers_report_bytes_wrapper },
    { "supervisor_query_returns_completed", supervisor_query_returns_completed_wrapper },
    { "supervisor_install_stages_package", supervisor_install_stages_package_wrapper },
    { "supervisor_enable_is_next_boot", supervisor_enable_is_next_boot_wrapper },
    { "supervisor_disable_loaded_module_is_next_boot", supervisor_disable_loaded_module_is_next_boot_wrapper },
    { "supervisor_disable_installed_module_is_next_boot", supervisor_disable_installed_module_is_next_boot_wrapper },
    { "supervisor_remove_is_next_boot", supervisor_remove_is_next_boot_wrapper },
    { "supervisor_remove_pending_slot_not_reclaimed_by_command", supervisor_remove_pending_slot_not_reclaimed_by_command_wrapper },
    { "supervisor_install_persists_disabled_module", supervisor_install_persists_disabled_module_wrapper },
    { "supervisor_registry_survives_reload", supervisor_registry_survives_reload_wrapper },
    { "supervisor_registry_restore_keeps_disabled_modules_unloaded", supervisor_registry_restore_keeps_disabled_modules_unloaded_wrapper },
    { "supervisor_registry_remove_intent_clears_at_boot", supervisor_registry_remove_intent_clears_at_boot_wrapper },
    { "supervisor_registry_corrupt_magic_is_ignored", supervisor_registry_corrupt_magic_is_ignored_wrapper },
    { "supervisor_resident_disable_is_reboot_required", supervisor_resident_disable_is_reboot_required_wrapper },
    { "supervisor_resident_remove_is_remove_pending", supervisor_resident_remove_is_remove_pending_wrapper },
    { "supervisor_unknown_slot_disallowed", supervisor_unknown_slot_disallowed_wrapper },
    { "supervisor_safe_mode_sets_flag", supervisor_safe_mode_sets_flag_wrapper },
    { "supervisor_safe_mode_rejects_activation", supervisor_safe_mode_rejects_activation_wrapper },
    { "supervisor_safe_mode_allows_next_boot_disable_remove", supervisor_safe_mode_allows_next_boot_disable_remove_wrapper },
    { "supervisor_safe_mode_allows_resident_next_boot", supervisor_safe_mode_allows_resident_next_boot_wrapper },
    { "supervisor_boot_markers_drive_safe_mode", supervisor_boot_markers_drive_safe_mode_wrapper },
    { "supervisor_crash_counter_saturates", supervisor_crash_counter_saturates_wrapper },
    { "supervisor_render_status_rejects_null", supervisor_render_status_rejects_null_wrapper },
    { "supervisor_add_module_rejects_full_table", supervisor_add_module_rejects_full_table_wrapper },
    { "supervisor_status_reflects_remove_pending", supervisor_status_reflects_remove_pending_wrapper },
    { "supervisor_status_sequence_embedded_and_even", supervisor_status_sequence_embedded_and_even_wrapper },
    { "supervisor_command_advances_sequence", supervisor_command_advances_sequence_wrapper },
    { "supervisor_read_rejects_torn_snapshot", supervisor_read_rejects_torn_snapshot_wrapper },
    { "supervisor_read_staging_never_torn", supervisor_read_staging_never_torn_wrapper },
    { "supervisor_error_persists_until_next_command", supervisor_error_persists_until_next_command_wrapper },
    { "supervisor_bad_slot_sets_error", supervisor_bad_slot_sets_error_wrapper },
    { "supervisor_enable_never_loads_so_no_load_error", supervisor_enable_never_loads_so_no_load_error_wrapper },
    { "supervisor_unknown_op_sets_error", supervisor_unknown_op_sets_error_wrapper },
    { "v2_golden_query_device_request", v2_golden_query_device_request_wrapper },
    { "v2_decode_rejects_malformed", v2_decode_rejects_malformed_wrapper },
    { "v2_device_write_echoes_request_id", v2_device_write_echoes_request_id_wrapper },
    { "v2_request_tracked_in_pending", v2_request_tracked_in_pending_wrapper },
    { "v2_enable_by_module_id", v2_enable_by_module_id_wrapper },
    { "v2_unknown_magic_rejected", v2_unknown_magic_rejected_wrapper },
    { "v2_install_accepts_only_bounded_token", v2_install_accepts_only_bounded_token_wrapper },
};
#define SUPERVISOR_DEVICE_TESTS_LEN \
    (sizeof(supervisor_device_tests) / sizeof(supervisor_device_tests[0]))

int run_supervisor_device_tests(void)
{
    RUN_TESTS(supervisor_device_tests, SUPERVISOR_DEVICE_TESTS_LEN);
}
