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
static uint32_t g_now;
static int g_loads;

static int u_register(void *c) { (void)c; return 0; }
static int u_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    (void)c; (void)n; (void)cls;
    g_loads++;
    return g_load_fail[g_sup->modules[i].active_gen] ? -77 : CANOPUS_STATE_ACTIVE;
}
static int u_remove(void *c, uint32_t i) { (void)c; (void)i; return 0; }
static uint32_t u_now(void *c) { (void)c; return g_now; }
static int u_pump(void *c, void (*poll)(void *), void *data) { (void)c; (void)poll; (void)data; return 0; }
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
static int u_import(void *c, const char *id);
static const char *g_pkg_ids[4];

static int u_stage_verify(void *c, const char *t, uint32_t *v, uint32_t *l)
{
    (void)c;
    if (g_pkg_ids[0] != 0 && u_import(c, t) != 0) return -1;
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

/* Completed native staging pairs reported by the platform. */
static uint32_t g_pkg_versions[4];
static int g_pkg_import_fail[4];
static int u_list(void *c, uint32_t i, char *id)
{
    (void)c;
    if (i >= 4u || g_pkg_ids[i] == 0) return 1;
    strcpy(id, g_pkg_ids[i]);
    return 0;
}
static int u_import(void *c, const char *id)
{
    uint32_t i;
    (void)c;
    for (i = 0; i < 4u && g_pkg_ids[i] != 0; i++) {
        if (strcmp(g_pkg_ids[i], id) != 0) continue;
        if (g_pkg_import_fail[i]) {
            g_sup->error_code = (uint32_t)CANOPUS_SUP_ERR_STAGE_NO_TARGET;
            return -1;
        }
        g_next.present = 1; g_next.valid = 1; g_next.version = g_pkg_versions[i];
        g_next.lifecycle = CANOPUS_LIFECYCLE_REMOVABLE;
        return 0;
    }
    return -1;
}

static const struct canopus_sup_platform_v1 update_platform = {
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .register_device = u_register,
    .unregister_device = u_register,
    .load_module = u_load,
    .stage_package = u_stage,
    .remove_artifact = u_remove,
    .persist = u_persist,
    .restore = u_restore,
    .stage_verify = u_stage_verify,
    .stage_commit = u_stage_commit,
    .gen_exists = u_gen_exists,
    .gen_verify = u_gen_verify,
    .list_packages = u_list,
    .monotonic_ms = u_now,
    .start_timer_pump = u_pump,
};

static void reset(struct canopus_supervisor_v1 *sup)
{
    memset(g_pkg_ids, 0, sizeof(g_pkg_ids));
    memset(g_gen, 0, sizeof(g_gen));
    memset(&g_next, 0, sizeof(g_next));
    memset(g_load_fail, 0, sizeof(g_load_fail));
    g_registry_present = 0;
    g_persist_fail = 0;
    g_now = 1000u;
    g_loads = 0;
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

/* A device reset keeps nothing but the registry file. */
static void power_cycle(struct canopus_supervisor_v1 *boot)
{
    canopus_supervisor_init(boot, 7, &update_platform, 0);
    g_sup = boot;
    CHECK_EQ(canopus_supervisor_restore_registry(boot), 0);
}

static void install_enabled_v1(struct canopus_supervisor_v1 *sup)
{
    install_v1(sup);
    sup->modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    CHECK_EQ(canopus_supervisor_save_registry(sup), 0);
}

TEST(module_that_never_stays_up_is_disabled_on_the_third_boot)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    g_loads = 0;
    power_cycle(&boot);
    power_cycle(&boot);
    CHECK_EQ(g_loads, 2); /* both starts ran the module and ended in a reset */
    power_cycle(&boot);
    CHECK_EQ(g_loads, 2);
    CHECK_EQ(boot.modules[0].intent, (uint32_t)CANOPUS_SUP_INTENT_DISABLED);
    CHECK_EQ((int32_t)boot.modules[0].activation_error, CANOPUS_SUP_ERR_UNSTABLE);
    CHECK_EQ(boot.crash_counter, 1u);
    CHECK_EQ(boot.failing_module_id, 1u);
    power_cycle(&boot);
    CHECK_EQ(g_loads, 2); /* the quarantine is persisted */
}

TEST(module_that_stays_up_keeps_loading)
{
    struct canopus_supervisor_v1 sup, boot;
    uint32_t i;
    install_enabled_v1(&sup);
    g_loads = 0;
    for (i = 0; i < 4u; i++) {
        power_cycle(&boot);
        g_now += CANOPUS_SUP_STABLE_MS - 1u;
        canopus_supervisor_stability_poll(&boot, g_now);
        CHECK_EQ(boot.modules[0].starts, 1u);
        g_now += 1u;
        canopus_supervisor_stability_poll(&boot, g_now);
        CHECK_EQ(boot.modules[0].starts, 0u);
    }
    CHECK_EQ(g_loads, 4);
    CHECK_EQ(boot.modules[0].intent, (uint32_t)CANOPUS_SUP_INTENT_ENABLED);
    CHECK_EQ(boot.crash_counter, 0u);
}

TEST(unstable_update_rolls_back_to_the_older_generation)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    power_cycle(&boot);
    CHECK_EQ(boot.modules[0].version, 2u);
    power_cycle(&boot);
    power_cycle(&boot);
    CHECK_EQ(boot.modules[0].version, 1u);
    CHECK_EQ(boot.modules[0].active_gen, 0u);
    CHECK_EQ(boot.modules[0].intent, (uint32_t)CANOPUS_SUP_INTENT_ENABLED);
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

TEST(scan_installs_new_packages_and_stages_updates)
{
    struct canopus_supervisor_v1 sup;
    reset(&sup);
    memset(g_pkg_ids, 0, sizeof(g_pkg_ids));
    g_pkg_ids[0] = "mod.hello"; g_pkg_versions[0] = 1;
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_SCAN, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.module_count, 1u);
    CHECK_EQ(sup.modules[0].version, 1u);
    CHECK_EQ(sup.scan_summary & 0xffu, 1u);          /* new */
    CHECK_EQ(sup.scan_summary >> 24, 1u);            /* scanned */

    /* same version again: already current, not a failure */
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_SCAN, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.scan_summary & 0xffffffu, 0u);
    CHECK_EQ(sup.error_code, 0u);

    /* a newer package: staged as an update (reboot required) */
    g_pkg_versions[0] = 2;
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_SCAN, 0), CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK_EQ((sup.scan_summary >> 8) & 0xffu, 1u);
    CHECK(sup.modules[0].pending_switch);
}

TEST(scan_reports_packages_without_a_payload_for_this_firmware)
{
    struct canopus_supervisor_v1 sup;
    reset(&sup);
    memset(g_pkg_ids, 0, sizeof(g_pkg_ids));
    memset(g_pkg_import_fail, 0, sizeof(g_pkg_import_fail));
    g_pkg_ids[0] = "mod.other"; g_pkg_import_fail[0] = 1;
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_SCAN, 0), CANOPUS_RESULT_FAILED);
    CHECK_EQ((sup.scan_summary >> 16) & 0xffu, 1u);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_STAGE_NO_TARGET);
    CHECK_EQ(sup.module_count, 0u);
    memset(g_pkg_import_fail, 0, sizeof(g_pkg_import_fail));
}


/* ---- global boot marker: safe mode, its persistence and the way out ---- */

#define BOOT_WORD_OFFSET 12u

static uint32_t registry_boot_word(void)
{
    return (uint32_t)g_registry[BOOT_WORD_OFFSET] |
           ((uint32_t)g_registry[BOOT_WORD_OFFSET + 1u] << 8) |
           ((uint32_t)g_registry[BOOT_WORD_OFFSET + 2u] << 16) |
           ((uint32_t)g_registry[BOOT_WORD_OFFSET + 3u] << 24);
}

static void set_registry_boot_word(uint32_t word)
{
    g_registry[BOOT_WORD_OFFSET] = (uint8_t)word;
    g_registry[BOOT_WORD_OFFSET + 1u] = (uint8_t)(word >> 8);
    g_registry[BOOT_WORD_OFFSET + 2u] = (uint8_t)(word >> 16);
    g_registry[BOOT_WORD_OFFSET + 3u] = (uint8_t)(word >> 24);
}

TEST(a_clean_registry_is_a_healthy_first_boot)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    CHECK_EQ(registry_boot_word(), 0u);
    power_cycle(&boot);
    CHECK(!boot.safe_mode);
    CHECK_EQ(g_loads, 1);
    CHECK_EQ(registry_boot_word() & CANOPUS_SUP_BOOT_BOOTING, (uint32_t)CANOPUS_SUP_BOOT_BOOTING);
}

TEST(a_boot_that_proves_every_module_stable_commits_ok)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    power_cycle(&boot);
    CHECK(registry_boot_word() & CANOPUS_SUP_BOOT_BOOTING);
    g_now += CANOPUS_SUP_STABLE_MS;
    canopus_supervisor_stability_poll(&boot, g_now);
    CHECK_EQ(registry_boot_word(), 0u);
    CHECK_EQ(boot.boot_state, (uint32_t)CANOPUS_BOOT_OK);
}

TEST(unfinished_boots_in_a_row_start_safe_mode_and_it_sticks)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_BOOT_BOOTING | (CANOPUS_SUP_UNFINISHED_LIMIT - 1u));
    power_cycle(&boot);
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_UNFINISHED_BOOT);
    CHECK_EQ(g_loads, 0); /* nothing third-party ran */
    power_cycle(&boot);   /* the verdict follows from the marker, so it holds across resets */
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_UNFINISHED_BOOT);
    CHECK_EQ(g_loads, 0);
}

TEST(fewer_unfinished_boots_only_count)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_BOOT_BOOTING | (CANOPUS_SUP_UNFINISHED_LIMIT - 2u));
    power_cycle(&boot);
    CHECK(!boot.safe_mode);
    CHECK_EQ(boot.boot_unfinished, CANOPUS_SUP_UNFINISHED_LIMIT - 1u);
    CHECK_EQ(g_loads, 1);
}

TEST(module_quarantines_without_a_good_boot_start_safe_mode)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_CRASH_THRESHOLD << CANOPUS_SUP_BOOT_CRASH_SHIFT);
    power_cycle(&boot);
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_CRASH);
    CHECK_EQ(g_loads, 0);
}

TEST(a_quarantine_is_remembered_by_the_boot_that_made_it)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    power_cycle(&boot);
    power_cycle(&boot);
    power_cycle(&boot); /* third start: the module is quarantined */
    CHECK_EQ(boot.crash_counter, 1u);
    CHECK_EQ((registry_boot_word() >> CANOPUS_SUP_BOOT_CRASH_SHIFT) & 0xffu, 1u);
    CHECK(!boot.safe_mode);
}

TEST(an_unreadable_registry_is_store_corruption)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    g_registry[0] ^= 0xffu; /* magic */
    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK(canopus_supervisor_restore_registry(&boot) != 0);
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_STORE_CORRUPT);
}

TEST(an_older_registry_format_is_ignored_not_treated_as_corruption)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    g_registry[4] ^= 0xffu; /* version */
    canopus_supervisor_init(&boot, 7, &update_platform, 0);
    g_sup = &boot;
    CHECK(canopus_supervisor_restore_registry(&boot) != 0);
    CHECK(!boot.safe_mode);
    CHECK_EQ(g_loads, 0);
}

TEST(user_requested_safe_mode_survives_a_reset)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0), CANOPUS_RESULT_COMPLETED);
    power_cycle(&boot);
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_USER_REQUESTED);
    CHECK_EQ(g_loads, 0);
}

TEST(safe_mode_still_registers_the_manager_but_stages_no_package)
{
    struct canopus_supervisor_v1 sup;
    install_enabled_v1(&sup);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(g_loads, 0);
    /* the payload-free stages pass the policy (this fake platform has no Manager to
     * register, so the stage itself fails); a stage beyond 2 never does */
    CHECK(command(&sup, CANOPUS_SUP_CMD_INSTALL, 0) != CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_INSTALL, 3), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_SAFE_MODE);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_DISALLOWED);
}

TEST(exit_safe_mode_needs_safe_mode_and_a_known_action)
{
    struct canopus_supervisor_v1 sup;
    install_enabled_v1(&sup);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, CANOPUS_SAFE_EXIT_KEEP),
             CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_NOT_SAFE_MODE);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, 3), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_BAD_COMMAND);
    CHECK(sup.safe_mode);
}

TEST(exit_with_disable_all_boots_normally_with_no_module_loaded)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_BOOT_SAFE |
                           (CANOPUS_SAFE_MODE_UNFINISHED_BOOT << CANOPUS_SUP_BOOT_REASON_SHIFT));
    power_cycle(&boot);
    CHECK(boot.safe_mode);
    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, CANOPUS_SAFE_EXIT_DISABLE_ALL),
             CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(boot.safe_mode); /* this run stays safe */
    CHECK_EQ(boot.modules[0].intent, (uint32_t)CANOPUS_SUP_INTENT_DISABLED);
    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_ENABLE, 0), CANOPUS_RESULT_DISALLOWED);
    power_cycle(&boot);
    CHECK(!boot.safe_mode);
    CHECK_EQ(g_loads, 0);
    CHECK_EQ(registry_boot_word(), 0u); /* nothing to prove: the trial is over */
}

TEST(a_trial_boot_that_does_not_finish_returns_to_safe_mode)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_BOOT_SAFE |
                           (CANOPUS_SAFE_MODE_CRASH << CANOPUS_SUP_BOOT_REASON_SHIFT));
    power_cycle(&boot);
    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, CANOPUS_SAFE_EXIT_KEEP),
             CANOPUS_RESULT_REBOOT_REQUIRED);
    power_cycle(&boot);   /* the trial: the module runs again */
    CHECK(!boot.safe_mode);
    CHECK(boot.boot_trial);
    CHECK_EQ(g_loads, 1);
    power_cycle(&boot);   /* reset before it proved stable */
    CHECK(boot.safe_mode);
    CHECK_EQ(boot.safe_mode_reason, (uint32_t)CANOPUS_SAFE_MODE_UNFINISHED_BOOT);
    CHECK_EQ(g_loads, 1);
}

TEST(a_trial_boot_that_stays_up_clears_the_trial)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    set_registry_boot_word(CANOPUS_SUP_BOOT_SAFE |
                           (CANOPUS_SAFE_MODE_USER_REQUESTED << CANOPUS_SUP_BOOT_REASON_SHIFT));
    power_cycle(&boot);
    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, CANOPUS_SAFE_EXIT_KEEP),
             CANOPUS_RESULT_REBOOT_REQUIRED);
    power_cycle(&boot);
    CHECK(boot.boot_trial);
    g_now += CANOPUS_SUP_STABLE_MS;
    canopus_supervisor_stability_poll(&boot, g_now);
    CHECK(!boot.boot_trial);
    CHECK_EQ(registry_boot_word(), 0u);
    power_cycle(&boot);
    CHECK(!boot.safe_mode);
    CHECK_EQ(g_loads, 2);
}

TEST(rollback_all_asks_modules_with_an_older_generation_to_go_back)
{
    struct canopus_supervisor_v1 sup, boot;
    install_enabled_v1(&sup);
    put_next(2, 1);
    CHECK_EQ(install(&sup, "mod.hello"), CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK_EQ(command(&sup, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0), CANOPUS_RESULT_COMPLETED);
    power_cycle(&boot);
    CHECK(boot.safe_mode);
    CHECK(boot.modules[0].pending_switch); /* the staged update, not yet applied */
    CHECK_EQ(command(&boot, CANOPUS_SUP_CMD_EXIT_SAFE_MODE, CANOPUS_SAFE_EXIT_ROLLBACK_ALL),
             CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(!boot.modules[0].pending_switch); /* cancelled: the running generation stays */
    power_cycle(&boot);
    CHECK_EQ(boot.modules[0].version, 1u);
}

static struct test_registry supervisor_update_tests[] = {
    { "scan_installs_new_packages_and_stages_updates",
      scan_installs_new_packages_and_stages_updates_wrapper },
    { "scan_reports_packages_without_a_payload_for_this_firmware",
      scan_reports_packages_without_a_payload_for_this_firmware_wrapper },
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
    { "module_that_never_stays_up_is_disabled_on_the_third_boot",
      module_that_never_stays_up_is_disabled_on_the_third_boot_wrapper },
    { "module_that_stays_up_keeps_loading",
      module_that_stays_up_keeps_loading_wrapper },
    { "unstable_update_rolls_back_to_the_older_generation",
      unstable_update_rolls_back_to_the_older_generation_wrapper },
    { "pending_switch_to_invalid_generation_is_abandoned",
      pending_switch_to_invalid_generation_is_abandoned_wrapper },
    { "rollback_requires_previous_generation_and_switches_at_next_load",
      rollback_requires_previous_generation_and_switches_at_next_load_wrapper },
    { "a_clean_registry_is_a_healthy_first_boot",
      a_clean_registry_is_a_healthy_first_boot_wrapper },
    { "a_boot_that_proves_every_module_stable_commits_ok",
      a_boot_that_proves_every_module_stable_commits_ok_wrapper },
    { "unfinished_boots_in_a_row_start_safe_mode_and_it_sticks",
      unfinished_boots_in_a_row_start_safe_mode_and_it_sticks_wrapper },
    { "fewer_unfinished_boots_only_count",
      fewer_unfinished_boots_only_count_wrapper },
    { "module_quarantines_without_a_good_boot_start_safe_mode",
      module_quarantines_without_a_good_boot_start_safe_mode_wrapper },
    { "a_quarantine_is_remembered_by_the_boot_that_made_it",
      a_quarantine_is_remembered_by_the_boot_that_made_it_wrapper },
    { "an_unreadable_registry_is_store_corruption",
      an_unreadable_registry_is_store_corruption_wrapper },
    { "an_older_registry_format_is_ignored_not_treated_as_corruption",
      an_older_registry_format_is_ignored_not_treated_as_corruption_wrapper },
    { "user_requested_safe_mode_survives_a_reset",
      user_requested_safe_mode_survives_a_reset_wrapper },
    { "safe_mode_still_registers_the_manager_but_stages_no_package",
      safe_mode_still_registers_the_manager_but_stages_no_package_wrapper },
    { "exit_safe_mode_needs_safe_mode_and_a_known_action",
      exit_safe_mode_needs_safe_mode_and_a_known_action_wrapper },
    { "exit_with_disable_all_boots_normally_with_no_module_loaded",
      exit_with_disable_all_boots_normally_with_no_module_loaded_wrapper },
    { "a_trial_boot_that_does_not_finish_returns_to_safe_mode",
      a_trial_boot_that_does_not_finish_returns_to_safe_mode_wrapper },
    { "a_trial_boot_that_stays_up_clears_the_trial",
      a_trial_boot_that_stays_up_clears_the_trial_wrapper },
    { "rollback_all_asks_modules_with_an_older_generation_to_go_back",
      rollback_all_asks_modules_with_an_older_generation_to_go_back_wrapper },
};

int run_supervisor_update_tests(void)
{
    RUN_TESTS(supervisor_update_tests,
              sizeof(supervisor_update_tests) / sizeof(supervisor_update_tests[0]));
}
