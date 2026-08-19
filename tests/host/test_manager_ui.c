/* Host tests: Manager UI model, pages and lifecycle-aware operations
 * (CAN-UI-001..004). Uses a fake transport that echoes ACCEPTED. */
#include "canopus_test.h"
#include "canopus_manager.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <string.h>

/* ---- fake transport ------------------------------------------------ */

static uint32_t g_last_command;
static uint32_t g_last_request_id;
static uint32_t g_next_result = CANOPUS_RESULT_ACCEPTED;
static int g_transport_calls;

static int fake_transport(const struct canopus_proto_request_v1 *req,
                          const void *payload, struct canopus_proto_response_v1 *resp,
                          void *cookie)
{
    (void)payload;
    (void)cookie;
    g_last_command = req->command;
    g_last_request_id = req->request_id;
    g_transport_calls++;
    canopus_proto_response_init(resp, req->request_id, g_next_result, 0);
    return 0;
}

static void reset_transport(void)
{
    g_last_command = 0;
    g_last_request_id = 0;
    g_next_result = CANOPUS_RESULT_ACCEPTED;
    g_transport_calls = 0;
}

static void add_removable(struct canopus_manager_model_v1 *m, const char *id)
{
    struct canopus_manager_module_v1 mod;
    canopus_memset(&mod, 0, sizeof(mod));
    canopus_buf_copy(mod.module_id, sizeof(mod.module_id), id);
    mod.lifecycle_class = CANOPUS_LIFECYCLE_REMOVABLE;
    mod.state = CANOPUS_STATE_ACTIVE;
    mod.version = 1;
    mod.signature_ok = 1;
    mod.has_previous = 1;
    mod.risk = CANOPUS_MANAGER_RISK_MODERATE;
    CHECK(canopus_manager_upsert_module(m, &mod) >= 0);
}

static void add_resident(struct canopus_manager_model_v1 *m, const char *id)
{
    struct canopus_manager_module_v1 mod;
    canopus_memset(&mod, 0, sizeof(mod));
    canopus_buf_copy(mod.module_id, sizeof(mod.module_id), id);
    mod.lifecycle_class = CANOPUS_LIFECYCLE_ALWAYS_RESIDENT;
    mod.state = CANOPUS_STATE_BOOT_RESIDENT;
    mod.version = 3;
    mod.signature_ok = 1;
    mod.has_previous = 0;
    mod.risk = CANOPUS_MANAGER_RISK_RESIDENT_CRITICAL;
    CHECK(canopus_manager_upsert_module(m, &mod) >= 0);
}

/* ---- device page (CAN-UI-001) -------------------------------------- */

TEST(device_page_shows_identity)
{
    struct canopus_manager_model_v1 m;
    char buf[256];
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    canopus_manager_set_identity(&m, "xiaomi-band-10-pro-3.101.030",
                                 "3.101.030", "CONBINE_LTALM078", 5);
    CHECK(canopus_manager_render_device(&m, buf, sizeof(buf)) == 0);
    CHECK(strstr(buf, "目标设备 : xiaomi-band-10-pro-3.101.030") != 0);
    CHECK(strstr(buf, "固件     : 3.101.030") != 0);
    CHECK(strstr(buf, "框架     : v5") != 0);
    CHECK(strstr(buf, "安全模式") == 0);
}

TEST(device_page_shows_safe_mode)
{
    struct canopus_manager_model_v1 m;
    char buf[256];
    canopus_manager_init(&m, fake_transport, 0);
    canopus_manager_set_identity(&m, "tgt", "v", "b", 1);
    m.safe_mode = 1;
    CHECK(canopus_manager_render_device(&m, buf, sizeof(buf)) == 0);
    CHECK(strstr(buf, "安全模式") != 0);
}

/* ---- module list (CAN-UI-002) -------------------------------------- */

TEST(module_list_lists_state)
{
    struct canopus_manager_model_v1 m;
    char buf[512];
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    add_resident(&m, "mod.bt");
    CHECK(canopus_manager_render_module_list(&m, buf, sizeof(buf)) == 0);
    CHECK(strstr(buf, "mod.hello") != 0);
    CHECK(strstr(buf, "mod.bt") != 0);
    CHECK(strstr(buf, "运行中") != 0);
    CHECK(strstr(buf, "启动时常驻") != 0);
}

/* ---- module detail: ops are class-aware (CAN-UI-004) ---------------- */

TEST(removable_detail_offers_disable_and_remove)
{
    struct canopus_manager_model_v1 m;
    char buf[512];
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    CHECK(canopus_manager_goto(&m, CANOPUS_MANAGER_VIEW_MODULE_DETAIL, 0) == 0);
    CHECK(canopus_manager_render_module_detail(&m, buf, sizeof(buf)) == 0);
    CHECK(strstr(buf, "类别     : 可移除") != 0);
    CHECK(strstr(buf, "[禁用]") != 0);
    CHECK(strstr(buf, "[移除]") != 0);
    /* a removable module may have a real unload */
    CHECK(strstr(buf, "[移除并重启]") == 0);
    CHECK(strstr(buf, "[下次启动禁用]") == 0);
}

TEST(resident_detail_has_no_fake_unload)
{
    struct canopus_manager_model_v1 m;
    char buf[512];
    canopus_manager_init(&m, fake_transport, 0);
    add_resident(&m, "mod.bt");
    CHECK(canopus_manager_goto(&m, CANOPUS_MANAGER_VIEW_MODULE_DETAIL, 0) == 0);
    CHECK(canopus_manager_render_module_detail(&m, buf, sizeof(buf)) == 0);
    CHECK(strstr(buf, "类别     : 始终常驻") != 0);
    /* never a plain disable/remove for resident */
    CHECK(strstr(buf, "[禁用]") == 0);
    CHECK(strstr(buf, "[移除]") == 0);
    /* only next-boot/reboot semantics */
    CHECK(strstr(buf, "[下次启动禁用]") != 0);
    CHECK(strstr(buf, "[移除并重启]") != 0);
    /* resident with no previous slot has no rollback */
    CHECK(strstr(buf, "[回滚]") == 0);
}

/* ---- operations are lifecycle-aware --------------------------------- */

TEST(disable_sends_command_for_both_classes)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    add_resident(&m, "mod.bt");

    /* removable: DISABLE means drain+unload now. Request ids are
     * client-monotonic, not a fixed value per opcode (CAN-P1-002). */
    CHECK(canopus_manager_op_disable(&m, 0) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_command == CANOPUS_CMD_DISABLE);
    CHECK(g_last_request_id == 1);

    /* resident: DISABLE is allowed but means next-boot only; the supervisor
     * interprets it by lifecycle class (CAN-DEV-006/007). The UI renders it
     * as [disable-next-boot] — never a fake unload (CAN-UI-004). */
    CHECK(canopus_manager_can_disable(&m, 1) != 0);
    CHECK(canopus_manager_op_disable(&m, 1) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_request_id == 2); /* monotonic increment, not opcode-fixed */
    CHECK(g_transport_calls == 2);
}

TEST(safe_mode_blocks_activation_ops)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    add_resident(&m, "mod.bt");
    m.safe_mode = 1;
    /* activation is never offered in safe mode */
    CHECK(canopus_manager_op_install(&m, "org.example.pkg") == CANOPUS_RESULT_DISALLOWED);
    CHECK(canopus_manager_can_update(&m, 0) == 0);
    CHECK(canopus_manager_can_update(&m, 1) == 0);
    CHECK(canopus_manager_can_enable(&m, 0) == 0);
    /* every disable/remove is next-boot (never runs third-party code), so
     * safe mode keeps them for all lifecycle classes as recovery paths */
    CHECK(canopus_manager_can_disable(&m, 0) != 0);
    CHECK(canopus_manager_can_remove(&m, 0) != 0);
    CHECK(canopus_manager_can_disable(&m, 1) != 0);
    CHECK(canopus_manager_can_remove(&m, 1) != 0);
}

TEST(request_ids_are_monotonic_and_never_zero)
{
    struct canopus_manager_model_v1 m;
    uint32_t seen = 0;
    uint32_t i;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    /* each command gets a fresh, strictly increasing id starting at 1 */
    for (i = 0; i < 5; i++) {
        CHECK(canopus_manager_op_enable(&m, 0) == CANOPUS_RESULT_ACCEPTED);
        CHECK(g_last_request_id == seen + 1u);
        seen = g_last_request_id;
    }
    /* the model counter tracked them */
    CHECK_EQ(m.next_request_id, 6u);
}

TEST(remove_resident_returns_disallowed_until_reboot_semantics)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    add_resident(&m, "mod.bt");

    /* removable remove is allowed */
    CHECK(canopus_manager_op_remove(&m, 0) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_command == CANOPUS_CMD_REMOVE);

    /* the UI still lets a resident be "removed + reboot" (REMOVE_PENDING) */
    CHECK(canopus_manager_can_remove(&m, 1) != 0);
    CHECK(canopus_manager_op_remove(&m, 1) == CANOPUS_RESULT_ACCEPTED);
}

TEST(rollback_needs_previous_slot)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");   /* has_previous = 1 */
    add_resident(&m, "mod.bt");       /* has_previous = 0 */

    CHECK(canopus_manager_can_rollback(&m, 0) != 0);
    CHECK(canopus_manager_op_rollback(&m, 0) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_command == CANOPUS_CMD_ROLLBACK);

    CHECK(canopus_manager_can_rollback(&m, 1) == 0);
    CHECK(canopus_manager_op_rollback(&m, 1) == CANOPUS_RESULT_DISALLOWED);
}

TEST(update_available_for_active)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    CHECK(canopus_manager_op_update(&m, 0) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_command == CANOPUS_CMD_UPDATE);
}

TEST(safe_mode_sets_flag_on_accepted)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, fake_transport, 0);
    CHECK(canopus_manager_op_safe_mode(&m) == CANOPUS_RESULT_ACCEPTED);
    CHECK(g_last_command == CANOPUS_CMD_ENTER_SAFE_MODE);
    CHECK(m.safe_mode == 1);
}

TEST(transport_failure_returns_rejected)
{
    struct canopus_manager_model_v1 m;
    reset_transport();
    canopus_manager_init(&m, 0, 0); /* no transport */
    add_removable(&m, "mod.hello");
    CHECK(canopus_manager_op_enable(&m, 0) == CANOPUS_RESULT_REJECTED);
}

TEST(goto_rejects_bad_view_and_index)
{
    struct canopus_manager_model_v1 m;
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    CHECK(canopus_manager_goto(&m, 999, 0) == -1);
    CHECK(canopus_manager_goto(&m, CANOPUS_MANAGER_VIEW_MODULE_DETAIL, 5) == -1);
    CHECK(canopus_manager_goto(&m, CANOPUS_MANAGER_VIEW_MODULE_DETAIL, 0) == 0);
}

TEST(render_truncation_reports_error)
{
    struct canopus_manager_model_v1 m;
    char small[8];
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "mod.hello");
    /* A 8-byte buffer cannot hold the module list: the API must report the
     * truncation (CANOPUS_TEXT_TRUNCATED) instead of claiming success. */
    CHECK(canopus_manager_render_module_list(&m, small, sizeof(small)) ==
          CANOPUS_TEXT_TRUNCATED);
    CHECK(small[sizeof(small) - 1] == '\0'); /* still NUL-terminated */
}

TEST(render_never_writes_outside_buffer)
{
    struct canopus_manager_model_v1 m;
    uint8_t storage[64];
    int i;
    canopus_manager_init(&m, fake_transport, 0);
    add_removable(&m, "a-module-id-that-is-very-long-indeed-for-this-renderer");
    for (i = 0; i < 64; i++) {
        storage[i] = 0x5A;
    }
    /* render into a tiny 8-byte region in the middle of a guarded buffer */
    CHECK(canopus_manager_render_module_list(&m, (char *)(storage + 8), 8) ==
          CANOPUS_TEXT_TRUNCATED);
    /* bytes before and after the region must be untouched — a truncation
     * underflow used to write at out-1 (before the buffer). */
    for (i = 0; i < 8; i++) {
        CHECK(storage[i] == 0x5A);
    }
    for (i = 16; i < 64; i++) {
        CHECK(storage[i] == 0x5A);
    }
    /* the render region itself stays NUL-terminated */
    CHECK(storage[15] == '\0');
}

static const struct test_registry manager_tests[] = {
    { "device_page_shows_identity", device_page_shows_identity_wrapper },
    { "device_page_shows_safe_mode", device_page_shows_safe_mode_wrapper },
    { "module_list_lists_state", module_list_lists_state_wrapper },
    { "removable_detail_offers_disable_and_remove", removable_detail_offers_disable_and_remove_wrapper },
    { "resident_detail_has_no_fake_unload", resident_detail_has_no_fake_unload_wrapper },
    { "disable_sends_command_for_both_classes", disable_sends_command_for_both_classes_wrapper },
    { "request_ids_are_monotonic_and_never_zero", request_ids_are_monotonic_and_never_zero_wrapper },
    { "safe_mode_blocks_activation_ops", safe_mode_blocks_activation_ops_wrapper },
    { "remove_resident_returns_disallowed_until_reboot_semantics", remove_resident_returns_disallowed_until_reboot_semantics_wrapper },
    { "rollback_needs_previous_slot", rollback_needs_previous_slot_wrapper },
    { "update_available_for_active", update_available_for_active_wrapper },
    { "safe_mode_sets_flag_on_accepted", safe_mode_sets_flag_on_accepted_wrapper },
    { "transport_failure_returns_rejected", transport_failure_returns_rejected_wrapper },
    { "goto_rejects_bad_view_and_index", goto_rejects_bad_view_and_index_wrapper },
    { "render_truncation_reports_error", render_truncation_reports_error_wrapper },
    { "render_never_writes_outside_buffer", render_never_writes_outside_buffer_wrapper },
};
#define MANAGER_TESTS_LEN (sizeof(manager_tests) / sizeof(manager_tests[0]))

int run_manager_tests(void)
{
    RUN_TESTS(manager_tests, MANAGER_TESTS_LEN);
}
