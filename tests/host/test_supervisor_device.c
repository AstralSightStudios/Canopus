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
static int g_unloads;
static int g_stages;
static int g_load_result = CANOPUS_STATE_ACTIVE;

static int fake_register(void *c) { (void)c; return 0; }
static int fake_unregister(void *c) { (void)c; return 0; }
static int fake_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    (void)c; (void)i; (void)n; (void)cls;
    g_loads++;
    return g_load_result;
}
static int fake_unload(void *c, uint32_t i) { (void)c; (void)i; g_unloads++; return 0; }
static int fake_stage(void *c, const char *p) { (void)c; (void)p; g_stages++; return 0; }

static const struct canopus_sup_platform_v1 fake_platform = {
    fake_register, fake_unregister, fake_load, fake_unload, fake_stage,
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

TEST(supervisor_enable_removable_loads_module)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_loads = 0;
    g_load_result = CANOPUS_STATE_ACTIVE;
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1) == 0);
    CHECK(sup.modules[0].state == CANOPUS_STATE_INSTALLED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_loads == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_ACTIVE);
}

TEST(supervisor_disable_removable_stops)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1) == 0);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED);
}

TEST(supervisor_remove_removable_unloads)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_unloads = 0;
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1) == 0);
    sup.modules[0].state = CANOPUS_STATE_ACTIVE;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_unloads == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_UNLOADED);
}

TEST(supervisor_resident_disable_is_reboot_required)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_unloads = 0;
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1) == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED_NEXT_BOOT);
    CHECK(g_unloads == 0); /* resident never unloads now */
}

TEST(supervisor_resident_remove_is_remove_pending)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_unloads = 0;
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, 1) == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
    CHECK(g_unloads == 0);
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
    canopus_supervisor_render_status(&sup, status);
    CHECK(r32(status, 12) == 1);
}

TEST(supervisor_add_module_rejects_full_table)
{
    struct canopus_supervisor_v1 sup;
    uint32_t i;
    int added = 0;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    for (i = 0; i < CANOPUS_SUP_MODULE_SLOTS + 2; i++) {
        if (canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, 1) >= 0) {
            added++;
        }
    }
    CHECK(added == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(sup.module_count == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(canopus_supervisor_add_module(&sup, 99, 1, 1) == -1); /* bad class */
}

static const struct test_registry supervisor_device_tests[] = {
    { "supervisor_status_abi_layout", supervisor_status_abi_layout_wrapper },
    { "supervisor_command_abi_validates", supervisor_command_abi_validates_wrapper },
    { "supervisor_device_transfers_report_bytes", supervisor_device_transfers_report_bytes_wrapper },
    { "supervisor_query_returns_completed", supervisor_query_returns_completed_wrapper },
    { "supervisor_install_stages_package", supervisor_install_stages_package_wrapper },
    { "supervisor_enable_removable_loads_module", supervisor_enable_removable_loads_module_wrapper },
    { "supervisor_disable_removable_stops", supervisor_disable_removable_stops_wrapper },
    { "supervisor_remove_removable_unloads", supervisor_remove_removable_unloads_wrapper },
    { "supervisor_resident_disable_is_reboot_required", supervisor_resident_disable_is_reboot_required_wrapper },
    { "supervisor_resident_remove_is_remove_pending", supervisor_resident_remove_is_remove_pending_wrapper },
    { "supervisor_unknown_slot_disallowed", supervisor_unknown_slot_disallowed_wrapper },
    { "supervisor_safe_mode_sets_flag", supervisor_safe_mode_sets_flag_wrapper },
    { "supervisor_add_module_rejects_full_table", supervisor_add_module_rejects_full_table_wrapper },
};
#define SUPERVISOR_DEVICE_TESTS_LEN \
    (sizeof(supervisor_device_tests) / sizeof(supervisor_device_tests[0]))

int run_supervisor_device_tests(void)
{
    RUN_TESTS(supervisor_device_tests, SUPERVISOR_DEVICE_TESTS_LEN);
}
