/* Host tests: device-side supervisor status/command ABI (Phase 5).
 * Drives canopus_supervisor.c through a fake platform and checks the exact
 * 384-byte status + 16-byte command ABI the Lua installer watchface uses. */
#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_module_registration.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <string.h>

/* ---- fake platform ------------------------------------------------- */

static int g_loads;
static int g_artifact_removals;
static int g_stages;
static uint32_t g_last_stage;
static int g_persists;
static int g_load_result = CANOPUS_STATE_ACTIVE;
static int g_stage_result = 0;
static int g_persist_result = 0;
static uint8_t g_registry[CANOPUS_SUP_REGISTRY_SIZE];
static int g_registry_present;
static int g_activations;
static int g_activation_result;
static int g_publications;
static uint32_t g_publication_stage;
static uint32_t g_publication_slot;
static int g_publication_result;
static int g_post_on_activate, g_activation_posts;
static void (*g_foreground_pump)(void *);
static void *g_foreground_data;
static void (*g_background_pump)(void *);
static void *g_background_data;
static struct canopus_supervisor_v1 *g_loading_supervisor;
static const struct canopus_module_descriptor_v1 *g_load_descriptor;

static void activation_ui_work(const struct canopus_context_v1 *ctx, void *user)
{
    (void)ctx; (void)user;
    g_activation_posts++;
}

static int fake_activate(const struct canopus_context_v1 *ctx)
{
    g_activations++;
    if (g_post_on_activate && g_activation_result == 0) {
        CHECK_EQ(ctx->ui->post(ctx, activation_ui_work, 0), 0);
    }
    return g_activation_result;
}
static int fake_module_callback(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}
static int fake_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static int fake_publish_native_app(const struct canopus_context_v1 *ctx)
{
    g_publications++;
    g_publication_slot = ctx->slot;
    g_publication_stage = 1u;
    return g_publication_result;
}

static int fake_publish_native_app_stage(
    const struct canopus_context_v1 *ctx, uint32_t stage)
{
    g_publications++;
    g_publication_slot = ctx->slot;
    g_publication_stage = stage;
    return g_publication_result;
}

static struct canopus_module_descriptor_v1 fake_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    0u,
    "mod.hello",
    "1.0.0",
    "host-test",
    "xiaomi-band-10-pro-3.101.030",
    fake_module_callback,
    fake_activate,
    fake_module_callback,
    fake_module_callback,
    fake_query,
    fake_publish_native_app,
    fake_publish_native_app_stage,
};

static int fake_register(void *c) { (void)c; return 0; }
static int fake_unregister(void *c) { (void)c; return 0; }
static int fake_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    (void)c; (void)n; (void)cls;
    g_loads++;
    if (g_loading_supervisor != 0 && g_load_descriptor != 0 &&
        i < CANOPUS_SUP_MODULE_SLOTS) {
        g_loading_supervisor->modules[i].descriptor = g_load_descriptor;
    }
    return g_load_result;
}
static int fake_remove_artifact(void *c, uint32_t i)
{
    (void)c; (void)i; g_artifact_removals++; return 0;
}
static int fake_stage(void *c, const char *p, uint32_t stage)
{
    (void)c; (void)p; g_stages++; g_last_stage = stage; return g_stage_result;
}
static int fake_persist(void *c, const uint8_t *d, uint32_t n)
{
    (void)c;
    if (d == 0 || n == 0u || n > sizeof(g_registry)) {
        return -1;
    }
    if (g_persist_result != 0) {
        return g_persist_result;
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

static uint32_t fake_l2_request_slots(void *cookie)
{
    (void)cookie;
    return 16u;
}

static const struct canopus_sup_platform_v1 fake_platform = {
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .register_device = fake_register,
    .unregister_device = fake_unregister,
    .load_module = fake_load,
    .stage_package = fake_stage,
    .remove_artifact = fake_remove_artifact,
    .persist = fake_persist,
    .restore = fake_restore,
};

static struct canopus_supervisor_v1 *g_event_supervisor;
static uint32_t g_module_events[CANOPUS_SUP_MODULE_SLOTS];
static struct canopus_module_state_v1 g_module_event;
static struct canopus_supervisor_v1 *g_refresh_supervisor;
static int g_native_refreshes, g_refresh_publications;
static uint32_t g_refresh_stage, g_refresh_error, g_refresh_event_error;

void canopus_manager_native_refresh(void)
{
    if (g_refresh_supervisor == 0) return;
    CHECK(CANOPUS_SNAPSHOT_READY(&g_refresh_supervisor->snap));
    g_native_refreshes++;
    g_refresh_publications = g_publications;
    g_refresh_stage = g_publication_stage;
    g_refresh_error = g_refresh_supervisor->modules[g_publication_slot].activation_error;
    g_refresh_event_error = g_module_event.activation_error;
}

static void on_registry_change(const struct canopus_context_v1 *ctx,
                                const char *topic, const void *payload,
                                uint32_t len, void *user)
{
    struct canopus_module_state_v1 queried;
    uint32_t slot;
    int publications = g_publications;
    (void)user;
    CHECK(strcmp(topic, CANOPUS_BUS_TOPIC_MODULE) == 0);
    CHECK_EQ(len, sizeof(g_module_event));
    memcpy(&g_module_event, payload, sizeof(g_module_event));
    CHECK(CANOPUS_SNAPSHOT_READY(&g_event_supervisor->snap));
    CHECK_EQ(ctx->bus->query_module(ctx, g_module_event.module_id, &queried), 0);
    CHECK(memcmp(&queried, &g_module_event, sizeof(queried)) == 0);
    for (slot = 0u; slot < CANOPUS_SUP_MODULE_SLOTS; slot++) {
        if (strcmp((const char *)g_event_supervisor->modules[slot].module_id,
                   g_module_event.module_id) == 0) break;
    }
    CHECK(slot < CANOPUS_SUP_MODULE_SLOTS);
    if (slot < CANOPUS_SUP_MODULE_SLOTS) g_module_events[slot]++;
    CHECK_EQ(ctx->bus->publish(ctx, "test/reentrant", 0, 0), CANOPUS_SVC_ERR_BUSY);
    CHECK_EQ(canopus_supervisor_scan_packages(g_event_supervisor), CANOPUS_RESULT_FAILED);
    CHECK_EQ(canopus_supervisor_publish_native_apps(g_event_supervisor, 1u), -1);
    CHECK_EQ(g_publications, publications);
}

static void observe_registry(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    const struct canopus_context_v1 *ctx = canopus_supervisor_context(sup, slot);
    g_event_supervisor = sup;
    memset(g_module_events, 0, sizeof(g_module_events));
    memset(&g_module_event, 0, sizeof(g_module_event));
    CHECK(ctx->bus->subscribe(ctx, CANOPUS_BUS_TOPIC_MODULE, on_registry_change, 0) >= 0);
}

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

TEST(module_registration_frame_requires_magic_not_only_size)
{
    uint8_t cpc2_query_module[CANOPUS_MODULE_REGISTRATION_SIZE];
    struct canopus_module_registration_v1 registration;
    canopus_memset(cpc2_query_module, 0, sizeof(cpc2_query_module));
    cpc2_query_module[0] = (uint8_t)CANOPUS_TRANSPORT_V2_MAGIC;
    cpc2_query_module[1] = (uint8_t)(CANOPUS_TRANSPORT_V2_MAGIC >> 8);
    cpc2_query_module[2] = (uint8_t)(CANOPUS_TRANSPORT_V2_MAGIC >> 16);
    cpc2_query_module[3] = (uint8_t)(CANOPUS_TRANSPORT_V2_MAGIC >> 24);
    CHECK(!canopus_module_registration_is_frame(
              cpc2_query_module, sizeof(cpc2_query_module)));

    canopus_memset(&registration, 0, sizeof(registration));
    registration.magic = CANOPUS_MODULE_REGISTRATION_MAGIC;
    CHECK(canopus_module_registration_is_frame(
              &registration, sizeof(registration)));
    CHECK(!canopus_module_registration_is_frame(
              &registration, sizeof(registration) - 1u));
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

TEST(supervisor_install_passes_legacy_publication_stages)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_stages = 0;
    g_last_stage = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 1, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_stages == 1);
    CHECK(g_last_stage == 1u);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 2, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_stages == 2);
    CHECK(g_last_stage == 2u);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 3, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_FAILED);
    CHECK(g_stages == 2);
}

TEST(supervisor_descriptor_registration_is_load_scoped)
{
    struct canopus_supervisor_v1 sup;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                        1, "mod.hello") == 0);
    CHECK(canopus_supervisor_register_descriptor(
              &sup, "mod.hello", &fake_descriptor) == -1);
    sup.loading_slot = 0;
    CHECK(canopus_supervisor_register_descriptor(
              &sup, "mod.hello", &fake_descriptor) == 0);
    CHECK(sup.modules[0].descriptor == &fake_descriptor);
    sup.loading_slot = -1;
}

TEST(supervisor_explicit_activate_targets_ready_slot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup,
              CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.loading_slot = 0;
    CHECK(canopus_supervisor_register_descriptor(
              &sup, "mod.hello", &fake_descriptor) == 0);
    sup.loading_slot = -1;
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_READY;
    g_activations = 0;
    g_activation_result = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ACTIVATE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) ==
          CANOPUS_RESULT_COMPLETED);
    CHECK(g_activations == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) ==
          CANOPUS_RESULT_DISALLOWED);
    CHECK(g_activations == 1);
}

TEST(supervisor_explicit_activation_reports_callback_failure)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                        1, "mod.hello") == 0);
    sup.loading_slot = 0;
    CHECK(canopus_supervisor_register_descriptor(
              &sup, "mod.hello", &fake_descriptor) == 0);
    sup.loading_slot = -1;
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_READY;
    g_activation_result = -77;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ACTIVATE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_FAILED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_FAILED);
    CHECK((int32_t)sup.modules[0].activation_error == -77);
    CHECK((int32_t)sup.error_code == CANOPUS_SUP_ERR_ACTIVATE);
    g_activation_result = 0;
}

TEST(supervisor_explicit_activation_loads_enabled_installed_slot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup,
                                        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
                                        1, "mod.hello") == 0);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_INSTALLED;
    g_loads = 0;
    g_load_result = CANOPUS_STATE_READY;
    g_loading_supervisor = &sup;
    g_load_descriptor = &fake_descriptor;
    g_activations = 0;
    g_activation_result = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ACTIVATE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK(g_loads == 1);
    CHECK(g_activations == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

static int capture_foreground_pump(void *cookie, void (*poll)(void *), void *data)
{
    (void)cookie;
    g_foreground_pump = poll;
    g_foreground_data = data;
    return 0;
}

static int capture_background_pump(void *cookie, void (*poll)(void *), void *data)
{
    (void)cookie;
    g_background_pump = poll;
    g_background_data = data;
    return 0;
}

static void setup_native_activation(struct canopus_supervisor_v1 *sup,
                                     struct canopus_sup_platform_v1 *platform,
                                     struct canopus_module_descriptor_v1 *descriptor)
{
    *platform = fake_platform;
    platform->start_timer_pump = capture_foreground_pump;
    platform->start_background_pump = capture_background_pump;
    *descriptor = fake_descriptor;
    descriptor->flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    g_foreground_pump = g_background_pump = 0;
    g_foreground_data = g_background_data = 0;
    g_publications = g_publication_result = 0;
    g_publication_stage = g_publication_slot = 0u;
    g_activations = g_activation_result = 0;
    g_post_on_activate = g_activation_posts = 0;
    g_loads = g_persists = g_persist_result = g_registry_present = 0;
    CHECK_EQ(canopus_supervisor_init(sup, 7u, platform, 0), 0);
    CHECK_EQ(canopus_supervisor_add_module(sup,
                 CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION, 1u, "mod.hello"), 0);
    sup->modules[0].state = CANOPUS_STATE_ENABLED;
    sup->modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    g_loading_supervisor = sup;
    g_load_descriptor = descriptor;
    g_load_result = CANOPUS_STATE_READY;
    g_refresh_supervisor = sup;
    g_native_refreshes = g_refresh_publications = 0;
    g_refresh_stage = g_refresh_error = g_refresh_event_error = 0u;
}

static uint32_t activation_command(struct canopus_supervisor_v1 *sup,
                                    uint32_t op, uint32_t slot)
{
    uint8_t command[CANOPUS_SUP_COMMAND_SIZE];
    make_command(command, CANOPUS_SUP_CMD_MAGIC, op, slot, 0u);
    return canopus_supervisor_handle_command(sup, command);
}

static void publication_foreground_turn(void)
{
    CHECK(g_foreground_pump != 0);
    if (g_foreground_pump != 0) g_foreground_pump(g_foreground_data);
}

static void finish_native_activation(void)
{
    g_refresh_supervisor = 0;
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
    g_load_result = CANOPUS_STATE_ACTIVE;
    g_post_on_activate = 0;
    g_activation_result = g_publication_result = 0;
}

TEST(supervisor_activation_publishes_on_separate_foreground_turns)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform;
    struct canopus_module_descriptor_v1 descriptor;
    setup_native_activation(&sup, &platform, &descriptor);
    /* A module's own post during activate must not occupy the publication queue. */
    g_post_on_activate = 1;
    CHECK_EQ(activation_command(&sup, CANOPUS_SUP_CMD_ACTIVATE, 0u),
             CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(g_loads, 1);
    CHECK_EQ(g_activations, 1);
    CHECK_EQ(sup.modules[0].state, CANOPUS_STATE_BOOT_RESIDENT);
    CHECK_EQ(g_publications, 0);
    CHECK_EQ(g_activation_posts, 0);
    CHECK_EQ(canopus_supervisor_start_background(&sup), 0);
    CHECK(g_background_pump != 0);
    if (g_background_pump != 0) g_background_pump(g_background_data);
    canopus_supervisor_timers_poll(&sup);
    CHECK_EQ(g_publications, 0);
    CHECK_EQ(g_activation_posts, 0);
    CHECK_EQ(g_native_refreshes, 0);
    publication_foreground_turn();
    CHECK_EQ(g_publications, 1);
    CHECK_EQ(g_publication_stage, 1u);
    CHECK_EQ(g_publication_slot, 0u);
    CHECK_EQ(g_activation_posts, 1);
    CHECK_EQ(g_native_refreshes, 1);
    CHECK_EQ(g_refresh_publications, 1);
    CHECK_EQ(g_refresh_stage, 1u);
    CHECK_EQ(g_refresh_error, 0u);
    canopus_supervisor_timers_poll(&sup);
    CHECK_EQ(g_publications, 1);
    CHECK_EQ(g_native_refreshes, 1);
    publication_foreground_turn();
    CHECK_EQ(g_publications, 2);
    CHECK_EQ(g_publication_stage, 2u);
    CHECK_EQ(g_native_refreshes, 2);
    CHECK_EQ(g_refresh_publications, 2);
    CHECK_EQ(g_refresh_stage, 2u);
    CHECK_EQ(g_refresh_error, 0u);
    publication_foreground_turn();
    CHECK_EQ(g_publications, 2);
    CHECK_EQ(g_native_refreshes, 2);
    finish_native_activation();
}

TEST(supervisor_activation_serializes_modules_and_supports_legacy_publication)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform;
    struct canopus_module_descriptor_v1 descriptor;
    uint32_t legacy, turn;
    for (legacy = 0u; legacy < 2u; legacy++) {
        uint32_t stages[2] = {0u, 0u};
        uint32_t expected = legacy ? 2u : 4u;
        setup_native_activation(&sup, &platform, &descriptor);
        if (legacy) descriptor.publish_native_app_stage = 0;
        CHECK_EQ(canopus_supervisor_add_module(&sup,
                     CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION, 1u, "mod.second"), 1);
        sup.modules[1].state = CANOPUS_STATE_ENABLED;
        sup.modules[1].intent = CANOPUS_SUP_INTENT_ENABLED;
        CHECK_EQ(activation_command(&sup, CANOPUS_SUP_CMD_ACTIVATE, 0u),
                 CANOPUS_RESULT_COMPLETED);
        CHECK_EQ(activation_command(&sup, CANOPUS_SUP_CMD_ACTIVATE, 1u),
                 CANOPUS_RESULT_COMPLETED);
        CHECK_EQ(g_activations, 2);
        CHECK_EQ(g_publications, 0);
        CHECK(g_foreground_pump != 0);
        for (turn = 0u; turn < 4u; turn++) {
            int previous = g_publications;
            publication_foreground_turn();
            CHECK(g_publications == previous || g_publications == previous + 1);
            if (g_publications != previous) {
                CHECK(g_publication_slot < 2u);
                if (g_publication_slot < 2u) {
                    stages[g_publication_slot]++;
                    CHECK_EQ(g_publication_stage, stages[g_publication_slot]);
                }
            }
        }
        CHECK_EQ(g_publications, expected);
        CHECK_EQ(stages[0], legacy ? 1u : 2u);
        CHECK_EQ(stages[1], legacy ? 1u : 2u);
        publication_foreground_turn();
        publication_foreground_turn();
        CHECK_EQ(g_publications, expected);
        finish_native_activation();
    }
}

TEST(supervisor_activation_publication_failure_is_persisted_and_notified)
{
    struct canopus_supervisor_v1 sup, restored;
    struct canopus_sup_platform_v1 platform;
    struct canopus_module_descriptor_v1 descriptor;
    uint32_t failure_stage, stage, events;
    int persists;
    for (failure_stage = 1u; failure_stage <= 2u; failure_stage++) {
        setup_native_activation(&sup, &platform, &descriptor);
        observe_registry(&sup, 0u);
        CHECK_EQ(activation_command(&sup, CANOPUS_SUP_CMD_ACTIVATE, 0u),
                 CANOPUS_RESULT_COMPLETED);
        events = g_module_events[0];
        persists = g_persists;
        for (stage = 1u; stage <= failure_stage; stage++) {
            g_publication_result = stage == failure_stage ? -100 : 0;
            publication_foreground_turn();
            CHECK_EQ(g_publications, stage);
            CHECK_EQ(g_publication_stage, stage);
            CHECK_EQ(g_native_refreshes, stage);
            CHECK_EQ(g_refresh_publications, stage);
            CHECK_EQ(g_refresh_stage, stage);
            CHECK_EQ((int32_t)g_refresh_error, g_publication_result);
        }
        CHECK_EQ((int32_t)sup.modules[0].activation_error, -100);
        CHECK_EQ((int32_t)g_refresh_event_error, -100);
        CHECK_EQ(g_module_events[0], events + 1u);
        CHECK_EQ((int32_t)g_module_event.activation_error, -100);
        CHECK(g_persists > persists);
        publication_foreground_turn();
        CHECK_EQ(g_publications, failure_stage);
        CHECK_EQ(g_native_refreshes, failure_stage);
        CHECK_EQ(g_module_events[0], events + 1u);
        CHECK_EQ(canopus_supervisor_init(&restored, 7u, &platform, 0), 0);
        CHECK_EQ(canopus_supervisor_restore_registry_metadata(&restored), 0);
        CHECK_EQ((int32_t)restored.modules[0].activation_error, -100);
        finish_native_activation();
    }
}

TEST(supervisor_activation_skips_failed_non_native_and_cancelled_publication)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform;
    struct canopus_module_descriptor_v1 descriptor;
    uint32_t scenario, published;
    for (scenario = 0u; scenario < 4u; scenario++) {
        setup_native_activation(&sup, &platform, &descriptor);
        if (scenario == 0u) g_activation_result = -77;
        if (scenario == 1u) descriptor.flags = 0u;
        CHECK_EQ(activation_command(&sup, CANOPUS_SUP_CMD_ACTIVATE, 0u),
                 scenario == 0u ? CANOPUS_RESULT_FAILED : CANOPUS_RESULT_COMPLETED);
        CHECK_EQ(g_publications, 0);
        if (scenario == 3u) {
            publication_foreground_turn();
            CHECK_EQ(g_publications, 1);
            CHECK_EQ(g_publication_stage, 1u);
        }
        if (scenario >= 2u) {
            CHECK_EQ(activation_command(&sup,
                         scenario == 2u ? CANOPUS_SUP_CMD_DISABLE : CANOPUS_SUP_CMD_REMOVE, 0u),
                     CANOPUS_RESULT_REBOOT_REQUIRED);
        }
        published = scenario == 3u ? 1u : 0u;
        /* Failed/non-native activations need no pump; start it to check that no
         * latent publication runs on a later unrelated foreground heartbeat. */
        CHECK_EQ(canopus_supervisor_ui_pump_ensure(&sup), 0);
        publication_foreground_turn();
        publication_foreground_turn();
        CHECK_EQ(g_publications, published);
        CHECK_EQ(g_native_refreshes, published);
        if (scenario == 0u) CHECK_EQ((int32_t)sup.modules[0].activation_error, -77);
        finish_native_activation();
    }
}

TEST(supervisor_restore_after_boot_command_loads_enabled_slots_once)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_REMOVABLE,
              1, "mod.disabled") == 1);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_INSTALLED;
    sup.modules[1].intent = CANOPUS_SUP_INTENT_DISABLED;
    sup.modules[1].state = CANOPUS_STATE_INSTALLED;
    g_loads = 0;
    g_load_result = CANOPUS_STATE_READY;
    g_loading_supervisor = &sup;
    g_load_descriptor = &fake_descriptor;
    g_activations = 0;
    g_activation_result = 0;
    g_persists = 0;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC,
                 CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT, 0, 0);

    CHECK(canopus_supervisor_handle_command(&sup, cmd) ==
          CANOPUS_RESULT_COMPLETED);
    CHECK(g_loads == 1);
    CHECK(g_activations == 1);
    CHECK(g_persists == 2); /* the start marker, then the final registry */
    CHECK(sup.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    CHECK(sup.modules[1].state == CANOPUS_STATE_INSTALLED);
    CHECK(sup.pending_op == CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT);

    CHECK(canopus_supervisor_handle_command(&sup, cmd) ==
          CANOPUS_RESULT_COMPLETED);
    CHECK(g_loads == 1);
    CHECK(g_activations == 1);
    CHECK(g_persists == 2);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

TEST(supervisor_restore_after_boot_command_reports_load_failure)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_INSTALLED;
    g_loads = 0;
    g_load_result = -77;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC,
                 CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT, 0, 0);

    CHECK(canopus_supervisor_handle_command(&sup, cmd) ==
          CANOPUS_RESULT_FAILED);
    CHECK(g_loads == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_FAILED);
    CHECK((int32_t)sup.modules[0].activation_error == -77);
    CHECK((int32_t)sup.error_code == CANOPUS_SUP_ERR_LOAD);
    g_load_result = CANOPUS_STATE_ACTIVE;
}

/* ---- CAN-P0-005 revision: next-boot lifecycle ----------------------- */

TEST(supervisor_enable_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_DISABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.modules[0].state == CANOPUS_STATE_DISABLED);
}

TEST(supervisor_remove_is_next_boot)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.one") == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.two") == 1);
    CHECK(sup.module_count == 2);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_REMOVE, 1, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_REBOOT_REQUIRED);
    CHECK(sup.module_count == 2); /* pending removal does not reclaim */
    CHECK(sup.modules[0].state == CANOPUS_STATE_REMOVE_PENDING);
    CHECK(sup.modules[1].state == CANOPUS_STATE_REMOVE_PENDING);
}

TEST(supervisor_persist_failure_rolls_back_enable)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                        1, "mod.hello") == 0);
    g_persist_result = CANOPUS_SUP_ERR_REGISTRY_RENAME;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_FAILED);
    CHECK((int32_t)sup.error_code == CANOPUS_SUP_ERR_REGISTRY_RENAME);
    CHECK(sup.modules[0].state == CANOPUS_STATE_INSTALLED);
    CHECK(sup.modules[0].intent == CANOPUS_SUP_INTENT_DISABLED);
    g_persist_result = 0;
}

TEST(supervisor_persist_failure_rolls_back_install)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                        1, "existing") == 0);
    g_persist_result = CANOPUS_SUP_ERR_REGISTRY_WRITE;
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_FAILED);
    CHECK((int32_t)sup.error_code == CANOPUS_SUP_ERR_REGISTRY_WRITE);
    CHECK(sup.module_count == 1);
    CHECK(strcmp((const char *)sup.modules[0].module_id, "existing") == 0);
    g_persist_result = 0;
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
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
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
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_REMOVABLE, 3, "mod.hello") == 0);
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

TEST(supervisor_registry_restore_auto_activates_ready_module)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a,
                                        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
                                        3, "mod.hello") == 0);
    a.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    a.modules[0].state = CANOPUS_STATE_ENABLED;
    CHECK(canopus_supervisor_save_registry(&a) == 0);

    g_loads = 0;
    g_load_result = CANOPUS_STATE_READY;
    g_loading_supervisor = &b;
    g_load_descriptor = &fake_descriptor;
    g_activations = 0;
    g_activation_result = 0;
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(g_loads == 1);
    CHECK(g_activations == 1);
    CHECK(b.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    CHECK(b.modules[0].activation_error == 0u);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

TEST(supervisor_registry_metadata_restore_defers_enabled_module_load)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a,
                                        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
                                        3, "mod.hello") == 0);
    a.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    a.modules[0].state = CANOPUS_STATE_ENABLED;
    CHECK(canopus_supervisor_save_registry(&a) == 0);

    g_loads = 0;
    g_load_result = CANOPUS_STATE_READY;
    g_loading_supervisor = &b;
    g_load_descriptor = &fake_descriptor;
    g_activations = 0;
    g_activation_result = 0;
    CHECK(canopus_supervisor_restore_registry_metadata(&b) == 0);
    CHECK(b.module_count == 1);
    CHECK(b.modules[0].intent == CANOPUS_SUP_INTENT_ENABLED);
    CHECK(b.modules[0].state == CANOPUS_STATE_INSTALLED);
    CHECK(g_loads == 0);
    CHECK(g_activations == 0);

    CHECK(canopus_supervisor_activate_restored_modules(&b) == 0);
    CHECK(g_loads == 1);
    CHECK(g_activations == 1);
    CHECK(b.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

TEST(supervisor_reactivates_opted_in_resident_module_after_second_restore)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    descriptor.flags = CANOPUS_FLAG_REACTIVATE_AFTER_UI_RESTART;
    CHECK(canopus_supervisor_add_module(&sup,
        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION, 1, "mod.hello") == 0);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    g_activations = 0;
    g_activation_result = 0;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == 0);
    CHECK(g_activations == 1);
    CHECK(sup.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    descriptor.flags = 0;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == 0);
    CHECK(g_activations == 1);
    descriptor.flags = CANOPUS_FLAG_REACTIVATE_AFTER_UI_RESTART;
    sup.modules[0].intent = CANOPUS_SUP_INTENT_DISABLED;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == 0);
    CHECK(g_activations == 1);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    g_activation_result = -42;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == -1);
    CHECK(sup.modules[0].activation_error == (uint32_t)-42);
    CHECK(sup.modules[0].state == CANOPUS_STATE_BOOT_RESIDENT);
    g_activation_result = 0;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == 0);
    CHECK(sup.modules[0].activation_error == 0);
    CHECK(canopus_supervisor_add_module(&sup,
        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION, 1, "mod.two") == 1);
    sup.modules[1].state = CANOPUS_STATE_INSTALLED;
    sup.modules[1].intent = CANOPUS_SUP_INTENT_ENABLED;
    g_loading_supervisor = &sup;
    g_load_descriptor = &fake_descriptor;
    g_load_result = CANOPUS_STATE_READY;
    g_loads = 0;
    g_activations = 0;
    CHECK(canopus_supervisor_activate_restored_modules(&sup) == 0);
    CHECK(g_loads == 1);
    CHECK(g_activations == 2);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

TEST(supervisor_registry_restores_multiple_pending_intents)
{
    struct canopus_supervisor_v1 a, b;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    g_persists = 0;
    CHECK(canopus_supervisor_add_module(
              &a, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.one") == 0);
    CHECK(canopus_supervisor_add_module(
              &a, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.two") == 1);
    CHECK(canopus_supervisor_add_module(
              &a, CANOPUS_LIFECYCLE_REMOVABLE,
              1, "mod.disabled") == 2);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&a, cmd) ==
          CANOPUS_RESULT_REBOOT_REQUIRED);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 1, 0);
    CHECK(canopus_supervisor_handle_command(&a, cmd) ==
          CANOPUS_RESULT_REBOOT_REQUIRED);

    g_loads = 0;
    g_load_result = CANOPUS_STATE_ACTIVE;
    g_loading_supervisor = &b;
    g_load_descriptor = &fake_descriptor;
    CHECK(canopus_supervisor_restore_registry_metadata(&b) == 0);
    CHECK(b.module_count == 3);
    CHECK(b.modules[0].intent == CANOPUS_SUP_INTENT_ENABLED);
    CHECK(b.modules[1].intent == CANOPUS_SUP_INTENT_ENABLED);
    CHECK(b.modules[2].intent == CANOPUS_SUP_INTENT_DISABLED);
    CHECK(g_loads == 0);

    CHECK(canopus_supervisor_activate_restored_modules(&b) == 0);
    CHECK(g_loads == 2);
    CHECK(b.modules[0].state == CANOPUS_STATE_ACTIVE);
    CHECK(b.modules[1].state == CANOPUS_STATE_ACTIVE);
    CHECK(b.modules[2].state == CANOPUS_STATE_INSTALLED);
    g_loading_supervisor = 0;
    g_load_descriptor = 0;
}

TEST(supervisor_registry_retains_boot_activation_error)
{
    struct canopus_supervisor_v1 a, b, c;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    canopus_supervisor_init(&c, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a,
                                        CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
                                        3, "mod.hello") == 0);
    a.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    a.modules[0].state = CANOPUS_STATE_ENABLED;
    CHECK(canopus_supervisor_save_registry(&a) == 0);

    g_load_result = CANOPUS_STATE_READY;
    g_loading_supervisor = &b;
    g_load_descriptor = &fake_descriptor;
    g_activation_result = -1102;
    CHECK(canopus_supervisor_restore_registry(&b) == 0);
    CHECK(b.modules[0].state == CANOPUS_STATE_FAILED);
    CHECK((int32_t)b.modules[0].activation_error == -1102);

    g_loading_supervisor = 0;
    g_load_descriptor = 0;
    g_load_result = CANOPUS_STATE_ACTIVE;
    CHECK(canopus_supervisor_restore_registry(&c) == 0);
    CHECK((int32_t)c.modules[0].activation_error == -1102);
    CHECK(c.modules[0].flags == 0u);
    g_activation_result = 0;
}

TEST(supervisor_native_app_publication_rejects_old_abi)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    descriptor.abi_major = 1u;
    descriptor.abi_minor = 2u;
    descriptor.struct_size = sizeof(descriptor);
    g_publications = 0;
    g_registry_present = 0;
    CHECK(canopus_supervisor_publish_native_apps(&sup, 1u) == 0);
    CHECK(canopus_supervisor_publish_native_apps(&sup, 2u) == 0);
    CHECK(g_publications == 0);
}

TEST(supervisor_single_callback_native_app_publishes_only_in_stage_one)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    descriptor.abi_minor = CANOPUS_ABI_MINOR;
    descriptor.struct_size = sizeof(descriptor);
    descriptor.publish_native_app_stage = 0;
    g_publications = 0;
    g_publication_result = 0;
    CHECK(canopus_supervisor_publish_native_apps(&sup, 1u) == 0);
    CHECK(g_publications == 1);
    CHECK(canopus_supervisor_publish_native_apps(&sup, 2u) == 0);
    CHECK(g_publications == 1);
}

TEST(supervisor_staged_native_app_receives_each_stage)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    g_publications = 0;
    g_publication_result = 0;
    CHECK(canopus_supervisor_publish_native_apps(&sup, 1u) == 0);
    CHECK(g_publications == 1);
    CHECK(g_publication_stage == 1u);
    CHECK(canopus_supervisor_publish_native_apps(&sup, 2u) == 0);
    CHECK(g_publications == 2);
    CHECK(g_publication_stage == 2u);
    CHECK(canopus_supervisor_publish_native_apps(&sup, 0u) == -1);
}

static int publication_install_stage(void *cookie, const char *package, uint32_t stage)
{
    if (package != 0) return -1;
    return canopus_supervisor_publish_native_apps(cookie, stage);
}
TEST(supervisor_publication_save_failure_retains_registry_diagnostic)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    struct canopus_sup_platform_v1 platform = fake_platform;
    uint8_t command[CANOPUS_SUP_COMMAND_SIZE];
    uint32_t stage;
    platform.stage_package = publication_install_stage;
    CHECK(canopus_supervisor_init(&sup, 7, &platform, &sup) == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
                                       1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    g_publications = 0;
    g_persist_result = CANOPUS_SUP_ERR_REGISTRY_WRITE;
    observe_registry(&sup, 0u);
    for (stage = 1; stage <= 2; stage++) {
        g_publication_result = stage == 1u ? -100 : 0;
        make_command(command, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, stage, 0);
        CHECK(canopus_supervisor_handle_command(&sup, command) == CANOPUS_RESULT_FAILED);
        CHECK((int32_t)sup.error_code == CANOPUS_SUP_ERR_REGISTRY_WRITE);
        CHECK(g_publications == (int)stage);
        CHECK(g_publication_stage == stage);
        CHECK(sup.modules[0].descriptor == &descriptor);
        CHECK_EQ(g_module_events[0], stage);
        CHECK_EQ(g_module_event.activation_error, g_publication_result);
        canopus_supervisor_module_events_flush(&sup);
        CHECK_EQ(g_module_events[0], stage);
    }
    g_persist_result = 0;
}

TEST(supervisor_publication_notifies_error_changes_and_retained_clear)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor;
    uint32_t staged, persist_fails;
    for (staged = 0u; staged < 2u; staged++) {
        for (persist_fails = 0u; persist_fails < 2u; persist_fails++) {
            uint32_t stage = staged ? 2u : 1u;
            descriptor = fake_descriptor;
            descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
            if (!staged) descriptor.publish_native_app_stage = 0;
            CHECK_EQ(canopus_supervisor_init(&sup, 7, &fake_platform, 0), 0);
            CHECK_EQ(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                                   1u, "mod.hello"), 0);
            sup.modules[0].state = CANOPUS_STATE_ACTIVE;
            sup.modules[0].descriptor = &descriptor;
            g_publications = 0;
            g_persist_result = persist_fails ? CANOPUS_SUP_ERR_REGISTRY_WRITE : 0;
            observe_registry(&sup, 0u);
            g_publication_result = -100;
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, 1u), -1);
            CHECK_EQ(g_module_events[0], 1u);
            CHECK_EQ(g_module_event.activation_error, -100);
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, 1u), -1);
            CHECK_EQ(g_module_events[0], 1u); /* unchanged errors stay silent */
            g_publication_result = -101;
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, stage), -1);
            CHECK_EQ(g_module_events[0], 2u);
            CHECK_EQ(g_module_event.activation_error, -101);
            g_publication_result = 0;
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, stage), persist_fails ? -1 : 0);
            CHECK_EQ(g_module_events[0], 3u);
            CHECK_EQ(g_module_event.activation_error, 0);
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, stage), persist_fails ? -1 : 0);
            CHECK_EQ(g_module_events[0], 3u); /* clearing zero stays silent */
            CHECK_EQ(g_publications, 5);
            CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, 0u), -1);
            CHECK_EQ(g_publications, 5);
            CHECK_EQ(g_module_events[0], 3u);
            if (persist_fails) CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_REGISTRY_WRITE);
        }
    }
    g_persist_result = 0;
}

TEST(supervisor_native_app_publication_persists_error)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = fake_descriptor;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(
              &sup, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION,
              1, "mod.hello") == 0);
    sup.modules[0].state = CANOPUS_STATE_BOOT_RESIDENT;
    sup.modules[0].descriptor = &descriptor;
    descriptor.flags = CANOPUS_FLAG_HAS_NATIVE_APP;
    g_publications = 0;
    g_publication_result = -100;
    g_registry_present = 0;
    g_persists = 0;
    CHECK(canopus_supervisor_publish_native_apps(&sup, 1u) == -1);
    CHECK(g_publications == 1);
    CHECK(g_publication_stage == 1u);
    CHECK((int32_t)sup.modules[0].activation_error == -100);
    CHECK(g_persists == 1);
    g_publication_result = 0;
}

static int scan_list_packages(void *cookie, uint32_t index, char *id)
{
    static const char *const ids[] = { "scan.new", "scan.update" };
    (void)cookie;
    if (index >= 2u) return 1;
    strcpy(id, ids[index]);
    return 0;
}

static int scan_verify_package(void *cookie, const char *id, uint32_t *version,
                                uint32_t *lifecycle)
{
    (void)cookie; (void)id;
    *version = 9u;
    *lifecycle = CANOPUS_LIFECYCLE_REMOVABLE;
    return 0;
}

static int scan_commit_package(void *cookie, const char *id, uint32_t generation)
{
    (void)cookie; (void)id; (void)generation;
    return 0;
}

TEST(supervisor_scan_notifies_retained_changes_on_save_failure)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform = fake_platform;
    struct canopus_module_state_v1 state;
    const struct canopus_context_v1 *ctx;
    uint8_t command[CANOPUS_SUP_COMMAND_SIZE];
    uint32_t via_command;
    platform.list_packages = scan_list_packages;
    platform.stage_verify = scan_verify_package;
    platform.stage_commit = scan_commit_package;
    for (via_command = 0u; via_command < 2u; via_command++) {
        CHECK_EQ(canopus_supervisor_init(&sup, 7, &platform, 0), 0);
        CHECK_EQ(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                               1u, "observer"), 0);
        CHECK_EQ(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE,
                                               1u, "scan.update"), 1);
        ctx = canopus_supervisor_context(&sup, 0u);
        observe_registry(&sup, 0u);
        g_persist_result = CANOPUS_SUP_ERR_REGISTRY_WRITE;
        make_command(command, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_SCAN, 0u, 0u);
        CHECK_EQ(via_command ? canopus_supervisor_handle_command(&sup, command)
                              : canopus_supervisor_scan_packages(&sup), CANOPUS_RESULT_FAILED);
        CHECK_EQ((int32_t)sup.error_code, CANOPUS_SUP_ERR_REGISTRY_WRITE);
        CHECK_EQ(sup.module_count, 3u);
        CHECK_EQ(g_module_events[0], 0u);
        CHECK_EQ(g_module_events[1], 1u);
        CHECK_EQ(g_module_events[2], 1u);
        CHECK_EQ(ctx->bus->query_module(ctx, "scan.new", &state), 0);
        CHECK_EQ(state.state, CANOPUS_STATE_INSTALLED);
        CHECK_EQ(state.version, 9u);
        CHECK_EQ(ctx->bus->query_module(ctx, "scan.update", &state), 0);
        CHECK_EQ(state.version, 1u); /* the new generation waits for reboot */
        CHECK((state.flags & CANOPUS_SUP_FLAG_UPDATE_PENDING) != 0u);
        g_persist_result = 0;
        CHECK_EQ(via_command ? canopus_supervisor_handle_command(&sup, command)
                              : canopus_supervisor_scan_packages(&sup), CANOPUS_RESULT_REBOOT_REQUIRED);
        CHECK_EQ(g_module_events[1], 1u); /* same pending update stays silent */
        CHECK_EQ(g_module_events[2], 1u); /* duplicate install stays silent */
        canopus_supervisor_module_events_flush(&sup);
        CHECK_EQ(g_module_events[1], 1u);
        CHECK_EQ(g_module_events[2], 1u);
    }
}

TEST(supervisor_registry_restore_keeps_disabled_modules_unloaded)
{
    struct canopus_supervisor_v1 a, b;
    canopus_supervisor_init(&a, 7, &fake_platform, 0);
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    g_registry_present = 0;
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, "mod.bt") == 0);
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
    CHECK(canopus_supervisor_add_module(&a, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, "mod.bt") == 0);
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

/* K11: a registry written by a pre-refactor Supervisor (version 1, signature
 * bit in flags) is not migrated: it is refused and nothing is loaded. */
TEST(supervisor_old_format_registry_is_refused_not_loaded)
{
    struct canopus_supervisor_v1 b;
    canopus_supervisor_init(&b, 7, &fake_platform, 0);
    canopus_memset(g_registry, 0, sizeof(g_registry));
    g_registry[0] = (uint8_t)(CANOPUS_SUP_REGISTRY_MAGIC & 0xffu);
    g_registry[1] = (uint8_t)((CANOPUS_SUP_REGISTRY_MAGIC >> 8) & 0xffu);
    g_registry[2] = (uint8_t)((CANOPUS_SUP_REGISTRY_MAGIC >> 16) & 0xffu);
    g_registry[3] = (uint8_t)((CANOPUS_SUP_REGISTRY_MAGIC >> 24) & 0xffu);
    g_registry[4] = 1u; /* version 1 */
    g_registry[8] = 1u; /* one module */
    canopus_memcpy(g_registry + CANOPUS_SUP_REGISTRY_HEADER, "mod.old", 8);
    g_registry[CANOPUS_SUP_REGISTRY_HEADER + 44] = (uint8_t)CANOPUS_SUP_INTENT_ENABLED;
    g_registry[CANOPUS_SUP_REGISTRY_HEADER + 40] = 1u; /* legacy signature_ok */
    g_registry_present = 1;
    g_loads = 0;
    CHECK(canopus_supervisor_restore_registry(&b) == -1);
    CHECK(b.module_count == 0);
    CHECK(g_loads == 0);
    g_registry_present = 0;
}

static void make_v2_request(uint8_t *buf, uint32_t cap, uint32_t opcode,
                            uint32_t request_id, const void *payload,
                            uint32_t payload_len);
static uint32_t v2_word(const uint8_t *b, uint32_t o);

TEST(v2_native_stage_and_restore_commands_replace_cpc1_framework_ops)
{
    struct canopus_supervisor_v1 sup;
    uint8_t wbuf[64], rbuf[64];
    uint8_t stage[4] = { 2, 0, 0, 0 };
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_stage_result = 0;
    g_stages = 0;
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_NATIVE_STAGE, 1, stage, 4);
    CHECK_EQ(canopus_supervisor_device_write(&sup, wbuf,
             CANOPUS_TRANSPORT_V2_HEADER_SIZE + 4), (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 4));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK_EQ(v2_word(rbuf, 28), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(g_stages, 1);
    CHECK_EQ(g_last_stage, 2u);
    stage[0] = 3; /* out of range */
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_NATIVE_STAGE, 2, stage, 4);
    CHECK_EQ(canopus_supervisor_device_write(&sup, wbuf,
             CANOPUS_TRANSPORT_V2_HEADER_SIZE + 4), (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 4));
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK_EQ(v2_word(rbuf, 28), CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(g_stages, 1);
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_RESTORE_AFTER_BOOT, 3, 0, 0);
    CHECK_EQ(canopus_supervisor_device_write(&sup, wbuf,
             CANOPUS_TRANSPORT_V2_HEADER_SIZE), (int32_t)CANOPUS_TRANSPORT_V2_HEADER_SIZE);
    CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
    CHECK_EQ(v2_word(rbuf, 28), CANOPUS_RESULT_COMPLETED);
}

TEST(supervisor_resident_disable_is_reboot_required)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, "mod.bt") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3, "mod.bt") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1,
                                        "mod.hello") == 0);
    uint32_t state_before = sup.modules[0].state;
    /* enter safe mode */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENTER_SAFE_MODE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    /* A package install (INSTALL beyond the three payload-free stages) and ENABLE
     * are rejected by policy, not just shown */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_INSTALL, 3, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    CHECK_EQ(sup.error_code, (uint32_t)CANOPUS_SUP_ERR_SAFE_MODE);
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ENABLE, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_DISALLOWED);
    /* RESTORE_AFTER_BOOT answers, but loads nothing */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC,
                 CANOPUS_SUP_CMD_RESTORE_AFTER_BOOT, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.modules[0].state, state_before);
    /* QUERY stays read-only and allowed */
    make_command(cmd, CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_QUERY, 0, 0);
    CHECK(canopus_supervisor_handle_command(&sup, cmd) == CANOPUS_RESULT_COMPLETED);
}

TEST(supervisor_safe_mode_allows_next_boot_disable_remove)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1,
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT, 3,
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

TEST(supervisor_status_exposes_first_module_callback_error)
{
    struct canopus_supervisor_v1 sup;
    uint8_t status[CANOPUS_SUP_STATUS_SIZE];
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    sup.modules[2].activation_error = (uint32_t)-100;
    sup.modules[5].activation_error = (uint32_t)-101;
    CHECK(canopus_supervisor_render_status(&sup, status) == 0);
    CHECK_EQ((int32_t)r32(status, CANOPUS_SUP_STATUS_MODULE_ERROR_OFF), -100);
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
        0x32,0x43,0x50,0x43, 0x24,0x00, 0x01,0x00, 0x02,0x00, 0x00,0x00,
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
    /* No L2 transport on this platform: the request depth is unknown. */
    CHECK(v2_word(rbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 84u) == 0u);
    CHECK(v2_word(rbuf, 0) == CANOPUS_TRANSPORT_V2_MAGIC);
    CHECK((v2_word(rbuf, 6) & 0xffffu) == CANOPUS_TRANSPORT_V2_RESPONSE);
    CHECK(v2_word(rbuf, 16) == CANOPUS_CMD_QUERY_DEVICE); /* opcode echoed */
    CHECK(v2_word(rbuf, 20) == 0x1234); /* request id echoed */
    CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_COMPLETED);
}

TEST(v2_local_transport_dispatches_without_device)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_proto_request_v1 req;
    struct canopus_proto_response_v1 resp;

    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    canopus_memset(&req, 0, sizeof(req));
    req.magic = CANOPUS_PROTO_MAGIC;
    req.struct_size = sizeof(req);
    req.abi_major = CANOPUS_ABI_MAJOR;
    req.abi_minor = CANOPUS_ABI_MINOR;
    req.command = CANOPUS_CMD_ENTER_SAFE_MODE;
    req.request_id = 0x91u;

    CHECK(canopus_supervisor_local_transport(&req, 0, &resp, &sup) == 0);
    CHECK_EQ(resp.request_id, req.request_id);
    CHECK_EQ(resp.result_state, CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(sup.safe_mode, 1u);
    CHECK(canopus_supervisor_local_transport(&req, 0, &resp, 0) == -1);
    req.request_id = 0u;
    CHECK(canopus_supervisor_local_transport(&req, 0, &resp, &sup) == -1);
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
    g_stages = 0;
    g_last_stage = 99u;
    make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_INSTALL, 0x77,
                    payload, 16);
    CHECK(canopus_supervisor_device_write(
              &sup, wbuf, CANOPUS_TRANSPORT_V2_HEADER_SIZE + 16) ==
          (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + 16));
    /* Mutations complete synchronously; their response is authoritative and
     * the terminal pending slot is immediately reusable. */
    CHECK(canopus_pending_find(&sup.pending, 0x77) == 0);
    CHECK(g_stages == 1);
    CHECK(g_last_stage == 0u); /* package INSTALL never becomes publication */
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1,
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

    /* Synchronous mutations must not exhaust the eight-slot request table. */
    {
        uint32_t request_id;
        for (request_id = 0x100u; request_id < 0x10Cu; request_id++) {
            make_v2_request(wbuf, sizeof(wbuf), CANOPUS_CMD_ENABLE,
                            request_id, payload, sizeof(payload));
            CHECK(canopus_supervisor_device_write(
                      &sup, wbuf,
                      CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)) ==
                  (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(payload)));
            CHECK(canopus_supervisor_device_read(&sup, rbuf, sizeof(rbuf)) > 0);
            CHECK(v2_word(rbuf, 28) == CANOPUS_RESULT_REBOOT_REQUIRED);
        }
    }

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

TEST(v2_l2_packet_handle_roundtrip)
{
    struct canopus_supervisor_v1 sup;
    uint8_t req_buf[CANOPUS_TRANSPORT_V2_HEADER_SIZE];
    uint8_t resp_buf[256];
    int32_t resp_len;
    struct canopus_proto_response_v1 resp;
    uint32_t opcode = 0, poff = 0;

    canopus_supervisor_init(&sup, 7, &fake_platform, 0);

    /* 1. NULL / invalid arguments fail */
    CHECK(canopus_supervisor_handle_l2_packet(0, req_buf, sizeof(req_buf), resp_buf, sizeof(resp_buf)) == -1);
    CHECK(canopus_supervisor_handle_l2_packet(&sup, 0, sizeof(req_buf), resp_buf, sizeof(resp_buf)) == -1);
    CHECK(canopus_supervisor_handle_l2_packet(&sup, req_buf, sizeof(req_buf), 0, sizeof(resp_buf)) == -1);
    CHECK(canopus_supervisor_handle_l2_packet(&sup, req_buf, 10, resp_buf, sizeof(resp_buf)) == -1);

    /* 2. Valid CPC2 QUERY_DEVICE over L2 */
    make_v2_request(req_buf, sizeof(req_buf), CANOPUS_CMD_QUERY_DEVICE, 0x4301, 0, 0);
    resp_len = canopus_supervisor_handle_l2_packet(&sup, req_buf, sizeof(req_buf), resp_buf, sizeof(resp_buf));
    CHECK_EQ(resp_len, (int32_t)(CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_QUERY_DEVICE_SIZE));

    CHECK(canopus_transport_v2_decode_response(resp_buf, (uint32_t)resp_len, &resp, &opcode, &poff) == 0);
    CHECK_EQ(opcode, (uint32_t)CANOPUS_CMD_QUERY_DEVICE);
    CHECK_EQ(resp.request_id, 0x4301u);
    CHECK_EQ(resp.result_state, (uint32_t)CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(resp.payload_size, (uint32_t)CANOPUS_QUERY_DEVICE_SIZE);
    CHECK_EQ(v2_word(resp_buf, poff), CANOPUS_QUERY_DEVICE_MAGIC);

    /* 3. Output buffer too small returns -1 */
    CHECK(canopus_supervisor_handle_l2_packet(&sup, req_buf, sizeof(req_buf), resp_buf, 20) == -1);

    /* 4. The record ends with how many requests the L2 transport holds: the phone keeps that
     * many in flight. The 84 bytes before it are what older Supervisors send. */
    {
        struct canopus_sup_platform_v1 platform = fake_platform;
        platform.l2_request_slots = fake_l2_request_slots;
        canopus_supervisor_init(&sup, 7, &platform, 0);
        resp_len = canopus_supervisor_handle_l2_packet(&sup, req_buf, sizeof(req_buf),
                                                       resp_buf, sizeof(resp_buf));
        CHECK(canopus_transport_v2_decode_response(resp_buf, (uint32_t)resp_len, &resp, &opcode,
                                                   &poff) == 0);
        CHECK_EQ(resp.payload_size, 88u);
        CHECK_EQ(v2_word(resp_buf, poff + 84u), 16u);
    }
}

TEST(installer_endpoint_accepts_signed_package_flow_only)
{
    struct canopus_supervisor_v1 sup;
    uint8_t request[128], response[40], control[128];
    static const char token[] = "bluetooth_audio";
    const uint32_t len = CANOPUS_TRANSPORT_V2_HEADER_SIZE + sizeof(token);
    uint32_t command;
    canopus_supervisor_init(&sup, 7, &fake_platform, 0);
    g_stage_result = 0;
    g_stages = 0;
    CHECK(canopus_supervisor_installer_read(&sup, response, sizeof(response)) < 0);
    /* A private control request cannot overwrite the installer mailbox. */
    make_v2_request(control, sizeof(control), CANOPUS_CMD_ECHO, 90, 0, 0);
    CHECK_EQ(canopus_supervisor_device_write(&sup, control, 36), 36);
    make_v2_request(request, sizeof(request), CANOPUS_CMD_INSTALL, 123, token, sizeof(token));
    CHECK_EQ(canopus_supervisor_installer_write(&sup, request, len), (int32_t)len);
    CHECK_EQ(g_stages, 1);
    CHECK_EQ(canopus_supervisor_installer_read(&sup, response, sizeof(response)), 40);
    CHECK_EQ(v2_word(response, 20), 123u);
    CHECK_EQ(v2_word(response, 28), CANOPUS_RESULT_COMPLETED);
    CHECK_EQ(v2_word(response, 32), 4u);
    CHECK_EQ(v2_word(response, 36), 0u);
    CHECK_EQ(canopus_supervisor_device_read(&sup, control, sizeof(control)), 36);
    CHECK_EQ(v2_word(control, 20), 90u);
    CHECK(canopus_supervisor_installer_read(&sup, response, 39) < 0);
    for (command = CANOPUS_CMD_ECHO; command <= CANOPUS_CMD_ACTIVATE; command++) {
        if (command == CANOPUS_CMD_INSTALL) continue;
        make_v2_request(request, sizeof(request), command, 124, token, sizeof(token));
        CHECK(canopus_supervisor_installer_write(&sup, request, len) < 0);
        CHECK_EQ(g_stages, 1);
        CHECK(canopus_supervisor_installer_read(&sup, response, sizeof(response)) < 0);
    }
    make_v2_request(request, sizeof(request), CANOPUS_CMD_INSTALL, 125, "../bad", 7);
    CHECK(canopus_supervisor_installer_write(&sup, request, 43) < 0);
    make_v2_request(request, sizeof(request), CANOPUS_CMD_INSTALL, 126, 0, 0);
    CHECK(canopus_supervisor_installer_write(&sup, request, 36) < 0);
    canopus_memset(request, 0, sizeof(request));
    CHECK(canopus_supervisor_installer_write(&sup, request, 16) < 0);
    CHECK_EQ(g_stages, 1);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
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
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") == 0);
    CHECK(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.two") == 1);
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
        if (canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.hello") >= 0) {
            added++;
        }
    }
    CHECK(added == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(sup.module_count == CANOPUS_SUP_MODULE_SLOTS);
    CHECK(canopus_supervisor_add_module(&sup, 99, 1, "mod.bad") == -1); /* bad class */
}

TEST(native_publication_stage_two_waits_for_registration_and_skips_failure)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform;
    struct canopus_module_descriptor_v1 descriptor;
    setup_native_activation(&sup, &platform, &descriptor);
    sup.modules[0].state = CANOPUS_STATE_READY;
    sup.modules[0].descriptor = &descriptor;
    sup.modules[0].native_app_status = CANOPUS_SUP_NATIVE_APP_PENDING;
    /* The bootstrap may request stage 2 before the asynchronous queue drained. */
    CHECK_EQ(canopus_supervisor_publish_native_apps(&sup, 2u), 0);
    CHECK_EQ(g_publications, 0);
    CHECK_EQ(sup.modules[0].native_app_stage, 2u);
    canopus_supervisor_native_apps_poll(&sup);
    CHECK_EQ(g_publications, 0);
    CHECK_EQ(sup.modules[0].native_app_stage, 2u);
    sup.modules[0].native_app_status = CANOPUS_SUP_NATIVE_APP_READY;
    canopus_supervisor_native_apps_poll(&sup);
    CHECK_EQ(g_publications, 1);
    CHECK_EQ(g_publication_stage, 2u);
    sup.modules[0].native_app_status = CANOPUS_SUP_NATIVE_APP_FAILED;
    sup.modules[0].native_app_stage = 2u;
    canopus_supervisor_native_apps_poll(&sup);
    CHECK_EQ(g_publications, 1);
    CHECK_EQ(sup.modules[0].native_app_stage, 0u);
    CHECK_EQ((int32_t)sup.modules[0].activation_error, CANOPUS_SUP_ERR_LAUNCHER);
    finish_native_activation();
}

static uint32_t g_native_ids_in_use;
static int fake_native_app_id_in_use(void *cookie, uint16_t id)
{
    (void)cookie;
    return (g_native_ids_in_use & (1u << (id - CANOPUS_SUP_NATIVE_APP_FIRST_ID))) != 0u;
}

TEST(native_identity_survives_slot_compaction_and_reversed_publication)
{
    struct canopus_supervisor_v1 sup, restored;
    int persists;
    g_persist_result = g_registry_present = g_persists = 0;
    CHECK_EQ(canopus_supervisor_init(&sup, 7u, &fake_platform, 0), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "removed"), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "native.other"), 1);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "corona"), 2);
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 1u), 0xCC);
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 2u), 0xCD);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_REMOVE;
    CHECK_EQ(canopus_supervisor_save_registry(&sup), 0);

    CHECK_EQ(canopus_supervisor_init(&restored, 7u, &fake_platform, 0), 0);
    CHECK_EQ(canopus_supervisor_restore_registry_metadata(&restored), 0);
    CHECK_EQ(restored.module_count, 2u);
    CHECK(strcmp((const char *)restored.modules[0].module_id, "native.other") == 0);
    CHECK(strcmp((const char *)restored.modules[1].module_id, "corona") == 0);
    persists = g_persists;
    /* Corona now publishes first and both slots changed, but neither ID does. */
    CHECK_EQ(canopus_supervisor_native_app_id(&restored, 1u), 0xCD);
    CHECK_EQ(canopus_supervisor_native_app_id(&restored, 0u), 0xCC);
    CHECK_EQ(g_persists, persists);
    CHECK_EQ(canopus_supervisor_add_module(&restored, 0u, 1u, "new.native"), 2);
    CHECK_EQ(canopus_supervisor_native_app_id(&restored, 2u), 0xCE);
}

TEST(native_identity_reserves_existing_firmware_apps_and_persists_before_use)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 platform = fake_platform;
    platform.native_app_id_in_use = fake_native_app_id_in_use;
    g_persist_result = g_registry_present = g_persists = 0;
    g_native_ids_in_use = (1u << 0) | (1u << 2);
    CHECK_EQ(canopus_supervisor_init(&sup, 7u, &platform, 0), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "corona"), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "native.other"), 1);
    g_persist_result = -5;
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 0u), CANOPUS_SVC_ERR_IO);
    CHECK_EQ(sup.modules[0].native_app_id, 0u);
    CHECK_EQ(g_persists, 0);
    g_persist_result = 0;
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 0u), 0xCD);
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 1u), 0xCF);
    CHECK_EQ(g_persists, 2);
    CHECK_EQ(g_registry[CANOPUS_SUP_REGISTRY_HEADER + 46u], 0xCD);
    CHECK_EQ(g_registry[CANOPUS_SUP_REGISTRY_HEADER + CANOPUS_SUP_REGISTRY_SLOT_SIZE + 46u], 0xCF);
    g_native_ids_in_use = 0xFFFFu;
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "no.room"), 2);
    CHECK_EQ(canopus_supervisor_native_app_id(&sup, 2u), CANOPUS_SVC_ERR_NOMEM);
    CHECK_EQ(sup.modules[2].native_app_id, 0u);
}

TEST(native_identity_imports_legacy_registry_and_rejects_duplicate_ownership)
{
    struct canopus_supervisor_v1 sup, restored;
    g_persist_result = g_registry_present = g_persists = 0;
    CHECK_EQ(canopus_supervisor_init(&sup, 7u, &fake_platform, 0), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "legacy"), 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, 0u, 1u, "second"), 1);
    CHECK_EQ(canopus_supervisor_save_registry(&sup), 0);
    CHECK_EQ(canopus_supervisor_init(&restored, 7u, &fake_platform, 0), 0);
    CHECK_EQ(canopus_supervisor_restore_registry_metadata(&restored), 0);
    CHECK_EQ(restored.modules[0].native_app_id, 0u);
    CHECK_EQ(canopus_supervisor_native_app_id(&restored, 0u), 0xCC);
    CHECK_EQ(canopus_supervisor_native_app_id(&restored, 1u), 0xCD);
    g_registry[CANOPUS_SUP_REGISTRY_HEADER + CANOPUS_SUP_REGISTRY_SLOT_SIZE + 46u] = 0xCC;
    CHECK_EQ(canopus_supervisor_init(&sup, 7u, &fake_platform, 0), 0);
    CHECK_EQ(canopus_supervisor_restore_registry_metadata(&sup), -1);
    CHECK_EQ(sup.safe_mode_reason, CANOPUS_SAFE_MODE_STORE_CORRUPT);
}

static const struct test_registry supervisor_device_tests[] = {
    { "native_publication_stage_two_waits_for_registration_and_skips_failure", native_publication_stage_two_waits_for_registration_and_skips_failure_wrapper },
    { "native_identity_survives_slot_compaction_and_reversed_publication", native_identity_survives_slot_compaction_and_reversed_publication_wrapper },
    { "native_identity_reserves_existing_firmware_apps_and_persists_before_use", native_identity_reserves_existing_firmware_apps_and_persists_before_use_wrapper },
    { "native_identity_imports_legacy_registry_and_rejects_duplicate_ownership", native_identity_imports_legacy_registry_and_rejects_duplicate_ownership_wrapper },
    { "installer_endpoint_accepts_signed_package_flow_only", installer_endpoint_accepts_signed_package_flow_only_wrapper },
    { "supervisor_init_rejects_null", supervisor_init_rejects_null_wrapper },
    { "supervisor_status_abi_layout", supervisor_status_abi_layout_wrapper },
    { "supervisor_command_abi_validates", supervisor_command_abi_validates_wrapper },
    { "module_registration_frame_requires_magic_not_only_size", module_registration_frame_requires_magic_not_only_size_wrapper },
    { "supervisor_query_returns_completed", supervisor_query_returns_completed_wrapper },
    { "supervisor_install_stages_package", supervisor_install_stages_package_wrapper },
    { "supervisor_install_passes_legacy_publication_stages", supervisor_install_passes_legacy_publication_stages_wrapper },
    { "supervisor_descriptor_registration_is_load_scoped", supervisor_descriptor_registration_is_load_scoped_wrapper },
    { "supervisor_explicit_activate_targets_ready_slot", supervisor_explicit_activate_targets_ready_slot_wrapper },
    { "supervisor_explicit_activation_reports_callback_failure", supervisor_explicit_activation_reports_callback_failure_wrapper },
    { "supervisor_explicit_activation_loads_enabled_installed_slot", supervisor_explicit_activation_loads_enabled_installed_slot_wrapper },
    { "supervisor_activation_publishes_on_separate_foreground_turns", supervisor_activation_publishes_on_separate_foreground_turns_wrapper },
    { "supervisor_activation_serializes_modules_and_supports_legacy_publication", supervisor_activation_serializes_modules_and_supports_legacy_publication_wrapper },
    { "supervisor_activation_publication_failure_is_persisted_and_notified", supervisor_activation_publication_failure_is_persisted_and_notified_wrapper },
    { "supervisor_activation_skips_failed_non_native_and_cancelled_publication", supervisor_activation_skips_failed_non_native_and_cancelled_publication_wrapper },
    { "supervisor_restore_after_boot_command_loads_enabled_slots_once", supervisor_restore_after_boot_command_loads_enabled_slots_once_wrapper },
    { "supervisor_restore_after_boot_command_reports_load_failure", supervisor_restore_after_boot_command_reports_load_failure_wrapper },
    { "supervisor_enable_is_next_boot", supervisor_enable_is_next_boot_wrapper },
    { "supervisor_disable_loaded_module_is_next_boot", supervisor_disable_loaded_module_is_next_boot_wrapper },
    { "supervisor_disable_installed_module_is_next_boot", supervisor_disable_installed_module_is_next_boot_wrapper },
    { "supervisor_remove_is_next_boot", supervisor_remove_is_next_boot_wrapper },
    { "supervisor_remove_pending_slot_not_reclaimed_by_command", supervisor_remove_pending_slot_not_reclaimed_by_command_wrapper },
    { "supervisor_persist_failure_rolls_back_enable", supervisor_persist_failure_rolls_back_enable_wrapper },
    { "supervisor_persist_failure_rolls_back_install", supervisor_persist_failure_rolls_back_install_wrapper },
    { "supervisor_install_persists_disabled_module", supervisor_install_persists_disabled_module_wrapper },
    { "supervisor_registry_survives_reload", supervisor_registry_survives_reload_wrapper },
    { "supervisor_registry_restore_auto_activates_ready_module", supervisor_registry_restore_auto_activates_ready_module_wrapper },
    { "supervisor_registry_metadata_restore_defers_enabled_module_load", supervisor_registry_metadata_restore_defers_enabled_module_load_wrapper },
    { "supervisor_reactivates_opted_in_resident_module_after_second_restore", supervisor_reactivates_opted_in_resident_module_after_second_restore_wrapper },
    { "supervisor_registry_retains_boot_activation_error", supervisor_registry_retains_boot_activation_error_wrapper },
    { "supervisor_registry_restores_multiple_pending_intents", supervisor_registry_restores_multiple_pending_intents_wrapper },
    { "supervisor_native_app_publication_rejects_old_abi", supervisor_native_app_publication_rejects_old_abi_wrapper },
    { "supervisor_single_callback_native_app_publishes_only_in_stage_one", supervisor_single_callback_native_app_publishes_only_in_stage_one_wrapper },
    { "supervisor_staged_native_app_receives_each_stage", supervisor_staged_native_app_receives_each_stage_wrapper },
    { "supervisor_native_app_publication_persists_error", supervisor_native_app_publication_persists_error_wrapper },
    { "supervisor_publication_save_failure_retains_registry_diagnostic", supervisor_publication_save_failure_retains_registry_diagnostic_wrapper },
    { "supervisor_publication_notifies_error_changes_and_retained_clear", supervisor_publication_notifies_error_changes_and_retained_clear_wrapper },
    { "supervisor_scan_notifies_retained_changes_on_save_failure", supervisor_scan_notifies_retained_changes_on_save_failure_wrapper },
    { "supervisor_registry_restore_keeps_disabled_modules_unloaded", supervisor_registry_restore_keeps_disabled_modules_unloaded_wrapper },
    { "supervisor_registry_remove_intent_clears_at_boot", supervisor_registry_remove_intent_clears_at_boot_wrapper },
    { "supervisor_registry_corrupt_magic_is_ignored", supervisor_registry_corrupt_magic_is_ignored_wrapper },
    { "v2_native_stage_and_restore_commands_replace_cpc1_framework_ops", v2_native_stage_and_restore_commands_replace_cpc1_framework_ops_wrapper },
    { "supervisor_old_format_registry_is_refused_not_loaded", supervisor_old_format_registry_is_refused_not_loaded_wrapper },
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
    { "supervisor_status_exposes_first_module_callback_error", supervisor_status_exposes_first_module_callback_error_wrapper },
    { "supervisor_add_module_rejects_full_table", supervisor_add_module_rejects_full_table_wrapper },
    { "supervisor_status_reflects_remove_pending", supervisor_status_reflects_remove_pending_wrapper },
    { "supervisor_status_sequence_embedded_and_even", supervisor_status_sequence_embedded_and_even_wrapper },
    { "supervisor_command_advances_sequence", supervisor_command_advances_sequence_wrapper },
    { "supervisor_error_persists_until_next_command", supervisor_error_persists_until_next_command_wrapper },
    { "supervisor_bad_slot_sets_error", supervisor_bad_slot_sets_error_wrapper },
    { "supervisor_enable_never_loads_so_no_load_error", supervisor_enable_never_loads_so_no_load_error_wrapper },
    { "supervisor_unknown_op_sets_error", supervisor_unknown_op_sets_error_wrapper },
    { "v2_golden_query_device_request", v2_golden_query_device_request_wrapper },
    { "v2_decode_rejects_malformed", v2_decode_rejects_malformed_wrapper },
    { "v2_device_write_echoes_request_id", v2_device_write_echoes_request_id_wrapper },
    { "v2_local_transport_dispatches_without_device", v2_local_transport_dispatches_without_device_wrapper },
    { "v2_request_tracked_in_pending", v2_request_tracked_in_pending_wrapper },
    { "v2_enable_by_module_id", v2_enable_by_module_id_wrapper },
    { "v2_unknown_magic_rejected", v2_unknown_magic_rejected_wrapper },
    { "v2_install_accepts_only_bounded_token", v2_install_accepts_only_bounded_token_wrapper },
    { "v2_l2_packet_handle_roundtrip", v2_l2_packet_handle_roundtrip_wrapper },
};
#define SUPERVISOR_DEVICE_TESTS_LEN \
    (sizeof(supervisor_device_tests) / sizeof(supervisor_device_tests[0]))

int run_supervisor_device_tests(void)
{
    RUN_TESTS(supervisor_device_tests, SUPERVISOR_DEVICE_TESTS_LEN);
}
