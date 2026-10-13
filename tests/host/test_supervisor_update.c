/* Host tests: two-generation module updates, rollback and fallback (P4). */
#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <string.h>

struct pkg {
    int present;
    int valid; /* receipt + artifact verify */
    uint32_t version;
    uint32_t lifecycle;
};

static struct pkg g_gen[2];
static struct pkg g_next;
static int g_load_fail[2]; /* generation that fails to load */
static uint8_t g_registry[CANOPUS_SUP_REGISTRY_SIZE];
static int g_registry_present;
static int g_persist_fail;
static struct canopus_supervisor_v1 *g_sup;

static int u_register(void *c) { (void)c; return 0; }
static int u_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    (void)c; (void)n; (void)cls;
    return g_load_fail[g_sup->modules[i].active_gen] ? -77 : CANOPUS_STATE_ACTIVE;
}
static int u_remove(void *c, uint32_t i) { (void)c; (void)i; return 0; }
static int u_stage(void *c, const char *token, uint32_t stage)
{
    (void)c; (void)stage;
    return canopus_supervisor_stage_token(g_sup, token);
}
static int u_persist(void *c, const uint8_t *d, uint32_t n)
{
    (void)c;
    if (g_persist_fail) return -1;
    canopus_memcpy(g_registry, d, n);
    g_registry_present = 1;
    return 0;
}
static int u_restore(void *c, uint8_t *d, uint32_t n)
{
    (void)c;
    if (!g_registry_present) return 1;
    canopus_memcpy(d, g_registry, n);
    return 0;
}
static int u_stage_verify(void *c, const char *t, uint32_t *v, uint32_t *l)
{
    (void)c; (void)t;
    if (!g_next.present || !g_next.valid) {
        g_sup->error_code = (uint32_t)CANOPUS_SUP_ERR_STAGE_ARTIFACT;
        return -1;
    }
    *v = g_next.version; *l = g_next.lifecycle;
    return 0;
}
static int u_stage_commit(void *c, const char *t, uint32_t gen)
{
    (void)c; (void)t;
    g_gen[gen] = g_next;
    memset(&g_next, 0, sizeof(g_next));
    return 0;
}
static int u_gen_exists(void *c, const char *t, uint32_t gen)
{
    (void)c; (void)t;
    return g_gen[gen].present;
}
static int u_gen_verify(void *c, const char *t, uint32_t gen, uint32_t *v, uint32_t *l)
{
    (void)c; (void)t;
    if (!g_gen[gen].present || !g_gen[gen].valid) return -1;
    *v = g_gen[gen].version; *l = g_gen[gen].lifecycle;
    return 0;
}

static const struct canopus_sup_platform_v1 update_platform = {
    "xiaomi-band-10-pro-3.101.030",
    u_register, u_register, u_load, u_stage, u_remove, u_persist, u_restore,
    u_stage_verify, u_stage_commit, u_gen_exists, u_gen_verify,
    0, 0, 0, 0, 0,
};

static void reset(struct canopus_supervisor_v1 *sup)
{
    memset(g_gen, 0, sizeof(g_gen));
    memset(&g_next, 0, sizeof(g_next));
    memset(g_load_fail, 0, sizeof(g_load_fail));
    g_registry_present = 0;
    g_persist_fail = 0;
    canopus_supervisor_init(sup, 7, &update_platform, 0);
    g_sup = sup;
}

static void put_next(uint32_t version, int valid)
{
    g_next.present = 1; g_next.valid = valid; g_next.version = version;
    g_next.lifecycle = CANOPUS_LIFECYCLE_REMOVABLE;
}

static uint32_t install(struct canopus_supervisor_v1 *sup, const char *token)
{
    uint8_t buf[128];
    uint8_t out[64];
    uint32_t n = (uint32_t)strlen(token) + 1u;
    uint32_t len = CANOPUS_TRANSPORT_V2_HEADER_SIZE + n;
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x32; buf[1] = 0x43; buf[2] = 0x50; buf[3] = 0x43;
    buf[4] = CANOPUS_TRANSPORT_V2_HEADER_SIZE;
    buf[6] = (uint8_t)CANOPUS_TRANSPORT_V2_REQUEST;
    buf[8] = (uint8_t)CANOPUS_ABI_MAJOR;
    buf[10] = (uint8_t)CANOPUS_ABI_MINOR;
    buf[12] = (uint8_t)len;
    buf[16] = (uint8_t)CANOPUS_CMD_INSTALL;
    buf[20] = 1;
    buf[32] = (uint8_t)n;
    memcpy(buf + CANOPUS_TRANSPORT_V2_HEADER_SIZE, token, n);
    CHECK_EQ(canopus_supervisor_installer_write(sup, buf, len), (int32_t)len);
    CHECK_EQ(canopus_supervisor_installer_read(sup, out, 40), 40);
    return (uint32_t)out[28] | ((uint32_t)out[29] << 8);
}

static uint32_t command(struct canopus_supervisor_v1 *sup, uint32_t op, uint32_t arg)
{
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint32_t words[4] = { CANOPUS_SUP_CMD_MAGIC, op, arg, 0 };
    memcpy(cmd, words, sizeof(cmd));
    return canopus_supervisor_handle_command(sup, cmd);
}

static void install_v1(struct canopus_supervisor_v1 *sup)
{
    reset(sup);
    put_next(1, 1);
    CHECK_EQ(install(sup, "mod.hello"), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup->modules[0].version, 1u);
    CHECK(g_gen[0].present && sup->modules[0].active_gen == 0u);
}

TEST(update_newer_version_is_staged_into_inactive_generation)
{
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(g_gen[1].present && g_gen[1].version == 2u);
    CHECK_EQ(g_gen[0].version, 1u);          /* running generation untouched */
    CHECK_EQ(sup.modules[0].version, 1u);    /* not active until the next load */
    CHECK(sup.modules[0].pending_switch);
    CHECK(sup.modules[0].flags & CANOPUS_SUP_FLAG_UPDATE_PENDING);
    CHECK(sup.modules[0].flags & CANOPUS_SUP_FLAG_HAS_PREV);
}

TEST(update_same_or_lower_version_is_rejected)
{
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    put_next(1, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_FAILED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_STAGE_DUPLICATE);
    CHECK(!g_gen[1].present);
    install_v1(&sup);
    put_next(5, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    command(&sup, CANOPUS_SUP_CMD_ROLLBACK, 0); /* cancel pending */
    put_next(3, 1);
    sup.modules[0].version = 4;
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_FAILED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_STAGE_DOWNGRADE);
}

TEST(update_with_bad_artifact_is_rejected_and_keeps_current)
{
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    put_next(2, 0);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_FAILED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_STAGE_ARTIFACT);
    CHECK(!g_gen[1].present);
    CHECK(!sup.modules[0].pending_switch);
}

TEST(update_persist_failure_restores_slot)
{
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    put_next(2, 1);
    g_persist_fail = 1;
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_FAILED);
    CHECK(!sup.modules[0].pending_switch);
}

TEST(pending_update_activates_at_next_load)
{
    struct canopus_supervisor_v1 boot;
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);

    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK_EQ(canopus_supervisor_restore_registry(&boot), 0);
    CHECK_EQ(boot.modules[0].version, 2u);
    CHECK_EQ(boot.modules[0].active_gen, 1u);
    CHECK(!boot.modules[0].pending_switch);
    CHECK_EQ(boot.modules[0].state, (uint32_t)CANOPUS_STATE_ACTIVE);
    CHECK(boot.modules[0].flags & CANOPUS_SUP_FLAG_HAS_PREV);
    /* the switch itself was persisted */
    canopus_supervisor_init(&sup, 7, &update_platform, 0);
    g_sup = &sup;
    CHECK_EQ(canopus_supervisor_restore_registry_metadata(&sup), 0);
    CHECK_EQ(sup.modules[0].version, 2u);
    CHECK_EQ(sup.modules[0].active_gen, 1u);
}

TEST(failed_load_after_update_falls_back_to_previous_generation)
{
    struct canopus_supervisor_v1 boot;
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    g_load_fail[1] = 1;

    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK_EQ(canopus_supervisor_restore_registry(&boot), 0);
    CHECK_EQ(boot.modules[0].version, 1u);
    CHECK_EQ(boot.modules[0].active_gen, 0u);
    CHECK_EQ(boot.modules[0].state, (uint32_t)CANOPUS_STATE_ACTIVE);
    CHECK_EQ((int32_t)boot.error_code, CANOPUS_SUP_ERR_UPDATE_FALLBACK);
}

TEST(pending_switch_to_invalid_generation_is_abandoned)
{
    struct canopus_supervisor_v1 boot;
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    g_gen[1].valid = 0; /* write interrupted / corrupted before the switch */

    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK_EQ(canopus_supervisor_restore_registry(&boot), 0);
    CHECK_EQ(boot.modules[0].version, 1u);
    CHECK_EQ(boot.modules[0].active_gen, 0u);
    CHECK(!boot.modules[0].pending_switch);
    CHECK_EQ(boot.modules[0].state, (uint32_t)CANOPUS_STATE_ACTIVE);
}

TEST(rollback_requires_previous_generation_and_switches_at_next_load)
{
    struct canopus_supervisor_v1 boot;
    struct canopus_supervisor_v1 sup;
    install_v1(&sup);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_ROLLBACK, 0), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_NO_PREV);

    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK_EQ(canopus_supervisor_restore_registry(&boot), 0);
    CHECK_EQ(boot.modules[0].version, 2u);

    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_ROLLBACK, 0), CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(boot.modules[0].pending_switch);
    canopus_supervisor_init(&sup, 7, &update_platform, 0);
    g_sup = &sup;
    CHECK_EQ(canopus_supervisor_restore_registry(&sup), 0);
    CHECK_EQ(sup.modules[0].version, 1u);
    CHECK_EQ(sup.modules[0].active_gen, 0u);
}

static struct test_registry supervisor_update_tests[] = {
    { "update_newer_version_is_staged_into_inactive_generation",
      update_newer_version_is_staged_into_inactive_generation_wrapper },
    { "update_same_or_lower_version_is_rejected",
      update_same_or_lower_version_is_rejected_wrapper },
    { "update_with_bad_artifact_is_rejected_and_keeps_current",
      update_with_bad_artifact_is_rejected_and_keeps_current_wrapper },
    { "update_persist_failure_restores_slot",
      update_persist_failure_restores_slot_wrapper },
    { "pending_update_activates_at_next_load",
      pending_update_activates_at_next_load_wrapper },
    { "failed_load_after_update_falls_back_to_previous_generation",
      failed_load_after_update_falls_back_to_previous_generation_wrapper },
    { "pending_switch_to_invalid_generation_is_abandoned",
      pending_switch_to_invalid_generation_is_abandoned_wrapper },
    { "rollback_requires_previous_generation_and_switches_at_next_load",
      rollback_requires_previous_generation_and_switches_at_next_load_wrapper },
};

int run_supervisor_update_tests(void)
{
    RUN_TESTS(supervisor_update_tests,
              sizeof(supervisor_update_tests) / sizeof(supervisor_update_tests[0]));
}
