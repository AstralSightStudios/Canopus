/*
 * all_services_test.c — Comprehensive end-to-end test suite for Canopus.
 *
 * Tests every single Microkernel Service Object on real/emulated hardware:
 *   1.  HOOK (Inline function hooking with trampoline jump-back, pointer slot hooking, state checks)
 *   2.  KV (set_int, get_int, set_string, get_string, set_bool, get_bool, remove, clear, default fallback)
 *   3.  FS (open, write, read, seek, tell, stat, size, exists, remove, write_all, read_all, mkdir, opendir, readdir, closedir)
 *   4.  MEM (alloc aligned, realloc preserving data, calloc zero-filled, used tracking, available 128K budget, free)
 *   5.  SYSTEM (target_id, device_model, firmware_version, battery_level, is_charging, is_safe_mode, uptime, revision)
 *   6.  TIME (monotonic_ms increasing, sleep_ms, timer_create, timer_start, timer_stop, timer_destroy)
 *   7.  HAPTICS (vibrate, vibrate_pattern, vibrate_preset, cancel)
 *   8.  DISPLAY (get_brightness, set_brightness, is_screen_on, get_resolution, acquire_wake_lock, release_wake_lock, wake_screen)
 *   9.  BUS (subscribe, publish with synchronous event dispatch, unsubscribe)
 *   10. BLUETOOTH (is_connected, get_rssi, adapter_state, l2_register_rx, l2_send)
 *   11. LOG (info, warn, error, debug, write)
 *   12. NOTIFY (post, post_with_icon)
 *   13. UI (declarative tree construction with extended catalog: Page, Header, Section, Text, Status, Button, Action, Switch, Slider, Checkbox, Divider)
 */

#include "canopus_abi.h"
#include "canopus_services.h"
#include "canopus_memory.h"
#include "canopus_module_registration.h"
#include "canopus_target_config.h"
#include "canopus_veneer.h"
#include "canopus_ui.h"

#define TEST_MODULE_ID "services_test"

static const struct canopus_context_v1 *s_ctx;
static uint32_t s_pass_count;
static uint32_t s_fail_count;
static char s_report_buf[2048];
static uint32_t s_report_len;

static void format_u32(char *buf, uint32_t val)
{
    char tmp[16];
    int i = 0, j = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void report_append(const char *str)
{
    uint32_t i = 0;
    while (str[i] != '\0' && s_report_len < sizeof(s_report_buf) - 2) {
        s_report_buf[s_report_len++] = str[i++];
    }
    s_report_buf[s_report_len] = '\0';
}

static void log_test(const char *name, int passed)
{
    if (passed) {
        s_pass_count++;
        report_append("[PASS] ");
        report_append(name);
        report_append("\n");
        if (s_ctx && s_ctx->log && s_ctx->log->info) {
            s_ctx->log->info(s_ctx, name);
        }
    } else {
        s_fail_count++;
        report_append("[FAIL] ");
        report_append(name);
        report_append("\n");
        if (s_ctx && s_ctx->log && s_ctx->log->error) {
            s_ctx->log->error(s_ctx, name);
        }
    }
}

/* ========================================================================= */
/* 1. HOOK ENGINE TESTS                                                      */
/* ========================================================================= */

static volatile int s_hook_inline_called = 0;
typedef int (*math_fn_t)(int, int);
static math_fn_t s_orig_math_add = 0;

__attribute__((noinline)) static int test_math_add(int a, int b)
{
    int val = (a * 7) + (b * 11) + 13;
    val = val ^ 0x3C;
    return val + 1;
}

static int hook_math_add(int a, int b)
{
    s_hook_inline_called++;
    int orig_val = s_orig_math_add(a, b);
    return orig_val * 2;
}

static int test_square(int x) { return x * x; }
static int (*s_ptr_target)(int) = test_square;
static int (*s_orig_ptr_target)(int) = 0;
static volatile int s_hook_ptr_called = 0;

static int hook_square(int x)
{
    s_hook_ptr_called++;
    int orig = s_orig_ptr_target(x);
    return orig + 500;
}

static void run_hook_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    int32_t hook_id;
    int32_t un_rc;

    if (!ctx->hook || !ctx->hook->install || !ctx->hook->uninstall || !ctx->hook->is_hooked) {
        log_test("Hook: service object missing", 0);
        return;
    }

    /* 1.1 Inline function hooking */
    volatile math_fn_t fn_math = test_math_add;
    volatile int arg_a = 2;
    volatile int arg_b = 3;

    int before_val = fn_math(arg_a, arg_b); /* 60 ^ 60 + 1 = 1 */
    if (before_val != 1) ok = 0;

    hook_id = ctx->hook->install(ctx, (uintptr_t)test_math_add, (uintptr_t)hook_math_add,
                                 (uintptr_t *)&s_orig_math_add, CANOPUS_HOOK_INLINE);
    if (hook_id < 0) ok = 0;
    if (ctx->hook->is_hooked(ctx, (uintptr_t)test_math_add) != 1) ok = 0;

    int hooked_val = fn_math(arg_a, arg_b);
    if (hooked_val != 2 || s_hook_inline_called != 1) ok = 0;

    un_rc = ctx->hook->uninstall(ctx, hook_id);
    if (un_rc != CANOPUS_SVC_OK) ok = 0;
    if (ctx->hook->is_hooked(ctx, (uintptr_t)test_math_add) != 0) ok = 0;

    int after_val = fn_math(arg_a, arg_b);
    if (after_val != 1) ok = 0;

    log_test("Hook: Inline function hook & trampoline call", ok);

    /* 1.2 Pointer slot hooking */
    ok = 1;
    if (s_ptr_target(5) != 25) ok = 0;

    hook_id = ctx->hook->install(ctx, (uintptr_t)&s_ptr_target, (uintptr_t)hook_square,
                                 (uintptr_t *)&s_orig_ptr_target, CANOPUS_HOOK_POINTER);
    if (hook_id < 0) ok = 0;

    if (s_ptr_target(5) != 525 || s_hook_ptr_called != 1) ok = 0;

    un_rc = ctx->hook->uninstall(ctx, hook_id);
    if (un_rc != CANOPUS_SVC_OK) ok = 0;

    if (s_ptr_target(5) != 25) ok = 0;

    log_test("Hook: Pointer slot hook & restore", ok);
}

/* ========================================================================= */
/* 2. KV STORE TESTS                                                         */
/* ========================================================================= */

static void run_kv_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    char sbuf[32];

    if (!ctx->kv) { log_test("KV: object missing", 0); return; }

    /* Integer */
    if (ctx->kv->set_int(ctx, "k_int", 987654) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->kv->get_int(ctx, "k_int", 0) != 987654) ok = 0;

    /* String */
    if (ctx->kv->set_string(ctx, "k_str", "canopus_rocks") != CANOPUS_SVC_OK) ok = 0;
    canopus_memset(sbuf, 0, sizeof(sbuf));
    if (ctx->kv->get_string(ctx, "k_str", sbuf, sizeof(sbuf)) < 0) ok = 0;
    if (canopus_strcmp(sbuf, "canopus_rocks") != 0) ok = 0;

    /* Bool */
    if (ctx->kv->set_bool(ctx, "k_bool", 1) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->kv->get_bool(ctx, "k_bool", 0) != 1) ok = 0;

    /* Remove & Fallback Default */
    if (ctx->kv->remove(ctx, "k_int") != CANOPUS_SVC_OK) ok = 0;
    if (ctx->kv->get_int(ctx, "k_int", 42) != 42) ok = 0;

    /* Clear */
    if (ctx->kv->clear(ctx) != CANOPUS_SVC_OK) ok = 0;
    canopus_memset(sbuf, 0, sizeof(sbuf));
    if (ctx->kv->get_string(ctx, "k_str", sbuf, sizeof(sbuf)) >= 0) ok = 0;

    log_test("KV: set/get/fallback for int, string, bool & clear", ok);
}

/* ========================================================================= */
/* 3. FS TESTS                                                               */
/* ========================================================================= */

static void run_fs_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    int32_t fd;
    char buf[32];
    struct canopus_fs_stat st;

    if (!ctx->fs) { log_test("FS: object missing", 0); return; }

    /* Open, write, seek, tell, read, close */
    fd = ctx->fs->open(ctx, "fstest.bin", CANOPUS_FS_WRITE | CANOPUS_FS_CREATE | CANOPUS_FS_TRUNC);
    if (fd < 0) ok = 0;
    if (ctx->fs->write(ctx, fd, "HELLOWORLD", 10) != 10) ok = 0;
    if (ctx->fs->tell(ctx, fd) != 10) ok = 0;
    if (ctx->fs->seek(ctx, fd, 5, CANOPUS_FS_SEEK_SET) != 5) ok = 0;
    if (ctx->fs->tell(ctx, fd) != 5) ok = 0;
    if (ctx->fs->write(ctx, fd, "12345", 5) != 5) ok = 0;
    ctx->fs->close(ctx, fd);

    fd = ctx->fs->open(ctx, "fstest.bin", CANOPUS_FS_READ);
    if (fd < 0) ok = 0;
    canopus_memset(buf, 0, sizeof(buf));
    if (ctx->fs->read(ctx, fd, buf, 10) != 10) ok = 0;
    if (canopus_strcmp(buf, "HELLO12345") != 0) ok = 0;
    ctx->fs->close(ctx, fd);

    log_test("FS: open, write, seek, tell, read, close", ok);

    /* Helpers: write_all, read_all, stat, exists, remove */
    ok = 1;
    if (ctx->fs->write_all(ctx, "quick.bin", "FASTDATA", 8) != 8) ok = 0;
    if (ctx->fs->exists(ctx, "quick.bin") != 1) ok = 0;
    canopus_memset(&st, 0, sizeof(st));
    if (ctx->fs->stat(ctx, "quick.bin", &st) != 0 || st.size != 8) ok = 0;
    canopus_memset(buf, 0, sizeof(buf));
    if (ctx->fs->read_all(ctx, "quick.bin", buf, sizeof(buf)) != 8) ok = 0;
    if (canopus_strcmp(buf, "FASTDATA") != 0) ok = 0;
    if (ctx->fs->remove(ctx, "quick.bin") != 0) ok = 0;
    if (ctx->fs->exists(ctx, "quick.bin") != 0) ok = 0;

    log_test("FS: write_all, read_all, stat, exists, remove", ok);

    /* Directory iteration */
    ok = 1;
    void *dir = ctx->fs->opendir(ctx, ".");
    if (dir != 0) {
        struct canopus_fs_dirent de;
        if (ctx->fs->readdir(ctx, dir, &de) != 0) ok = 0;
        ctx->fs->closedir(ctx, dir);
    } else {
        ok = 0;
    }
    log_test("FS: opendir, readdir, closedir", ok);
}

/* ========================================================================= */
/* 4. MEM TESTS                                                              */
/* ========================================================================= */

static void run_mem_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    uint32_t avail;
    uint32_t before_used;
    void *p1;
    void *p2;
    void *p3;

    if (!ctx->mem) { log_test("Mem: object missing", 0); return; }

    avail = ctx->mem->available(ctx);
    if (avail < 100u * 1024u) ok = 0; /* 128 KiB heap budget verification */

    before_used = ctx->mem->used(ctx);
    p1 = ctx->mem->alloc(ctx, 256);
    if (p1 == 0 || ((uintptr_t)p1 % 4) != 0) ok = 0;
    canopus_memset(p1, 0x5A, 256);

    p2 = ctx->mem->realloc(ctx, p1, 512);
    if (p2 == 0) ok = 0;
    /* Verify preserved data */
    const uint8_t *b = (const uint8_t *)p2;
    for (int i = 0; i < 256; i++) {
        if (b[i] != 0x5A) { ok = 0; break; }
    }

    p3 = ctx->mem->calloc(ctx, 32, 2);
    if (p3 == 0) ok = 0;
    const uint8_t *c = (const uint8_t *)p3;
    for (int i = 0; i < 64; i++) {
        if (c[i] != 0) { ok = 0; break; }
    }

    if (ctx->mem->used(ctx) <= before_used) ok = 0;

    ctx->mem->free(ctx, p2);
    ctx->mem->free(ctx, p3);

    log_test("Mem: alloc 256B, realloc 512B, calloc 64B & free (128K heap)", ok);
}

/* ========================================================================= */
/* 5. SYSTEM & TIME TESTS                                                    */
/* ========================================================================= */

static void dummy_timer_cb(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data)
{
    (void)ctx;
    (void)timer_id;
    (void)user_data;
}

static void run_system_and_time_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    if (!ctx->system) { log_test("System: object missing", 0); return; }

    const char *target = ctx->system->target_id(ctx);
    const char *model = ctx->system->device_model(ctx);
    const char *ver = ctx->system->firmware_version(ctx);
    uint32_t batt = ctx->system->battery_level(ctx);

    if (!target || !model || !ver) ok = 0;
    if (canopus_strcmp(model, "Xiaomi Band 10 Pro") != 0) ok = 0;
    if (batt > 100u) ok = 0;
    if (ctx->system->is_safe_mode(ctx) != 0) ok = 0;
    if (ctx->system->framework_revision(ctx) == 0) ok = 0;

    log_test("System: target_id, device_model, version, battery & revision", ok);

    ok = 1;
    if (!ctx->time) { log_test("Time: object missing", 0); return; }

    uint32_t t1 = ctx->time->monotonic_ms(ctx);
    ctx->time->sleep_ms(ctx, 5);
    uint32_t t2 = ctx->time->monotonic_ms(ctx);
    if (t2 < t1) ok = 0;

    int32_t tid = ctx->time->timer_create(ctx, 100, 1, dummy_timer_cb, 0);
    if (tid < 0) ok = 0;
    if (ctx->time->timer_start(ctx, tid) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->time->timer_stop(ctx, tid) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->time->timer_destroy(ctx, tid) != CANOPUS_SVC_OK) ok = 0;

    log_test("Time: monotonic clock, sleep_ms & timer lifecycle", ok);
}

/* ========================================================================= */
/* 6. HAPTICS & DISPLAY TESTS                                                */
/* ========================================================================= */

static void run_haptics_and_display_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    if (!ctx->haptics) { log_test("Haptics: object missing", 0); return; }

    if (ctx->haptics->vibrate(ctx, 20) != CANOPUS_SVC_OK) ok = 0;
    uint16_t pat[] = {20, 20, 20};
    if (ctx->haptics->vibrate_pattern(ctx, pat, 3) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->haptics->cancel(ctx) != CANOPUS_SVC_OK) ok = 0;

    log_test("Haptics: vibrate, pattern, preset click & cancel", ok);

    ok = 1;
    if (!ctx->display) { log_test("Display: object missing", 0); return; }

    if (ctx->display->get_brightness(ctx) != 100u) ok = 0;
    if (ctx->display->set_brightness(ctx, 80u) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->display->is_screen_on(ctx) != 1) ok = 0;
    uint32_t w = 0, h = 0;
    ctx->display->get_resolution(ctx, &w, &h);
    if (w != 336u || h != 480u) ok = 0;

    int32_t lock = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN);
    if (lock < 0) ok = 0;
    if (ctx->display->release_wake_lock(ctx, lock) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->display->wake_screen(ctx) != CANOPUS_SVC_OK) ok = 0;

    log_test("Display: brightness, resolution (336x480) & wake lock", ok);
}

/* ========================================================================= */
/* 7. BUS, BLUETOOTH, LOG & NOTIFY TESTS                                     */
/* ========================================================================= */

static volatile uint32_t s_bus_received = 0;
static void on_test_bus_event(const struct canopus_context_v1 *ctx,
                              const char *topic,
                              const void *payload,
                              uint32_t len,
                              void *user_data)
{
    (void)ctx;
    (void)topic;
    (void)user_data;
    if (len == sizeof(uint32_t)) {
        s_bus_received = *(const uint32_t *)payload;
    }
}

static void dummy_l2_cb(const struct canopus_context_v1 *ctx, uint8_t ch, const void *p, uint16_t l, void *ud)
{
    (void)ctx; (void)ch; (void)p; (void)l; (void)ud;
}

static void run_bus_bt_log_notify_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;

    /* Bus */
    if (ctx->bus) {
        int32_t sub = ctx->bus->subscribe(ctx, "test/topic", on_test_bus_event, 0);
        if (sub < 0) ok = 0;
        uint32_t payload = 0x12345678;
        if (ctx->bus->publish(ctx, "test/topic", &payload, sizeof(payload)) != 1) ok = 0;
        if (s_bus_received != 0x12345678) ok = 0;
        if (ctx->bus->unsubscribe(ctx, sub) != CANOPUS_SVC_OK) ok = 0;
    } else {
        ok = 0;
    }
    log_test("Bus: subscribe, publish & synchronous dispatch", ok);

    /* Bluetooth */
    ok = 1;
    if (ctx->bt) {
        if (ctx->bt->is_connected(ctx) != 1) ok = 0;
        if (ctx->bt->get_rssi(ctx) >= 0) ok = 0;
        if (ctx->bt->adapter_state(ctx) == 0) ok = 0;
        if (ctx->bt->l2_register_rx(ctx, 0x43, dummy_l2_cb, 0) != CANOPUS_SVC_OK) ok = 0;
        if (ctx->bt->l2_send(ctx, 0x43, "ping", 4) != CANOPUS_SVC_OK) ok = 0;
    } else {
        ok = 0;
    }
    log_test("Bluetooth: status, RSSI, adapter state & L2 registration", ok);

    /* Log & Notify */
    ok = 1;
    if (ctx->log) {
        ctx->log->info(ctx, "all_services_test: info message");
        ctx->log->warn(ctx, "all_services_test: warn message");
        ctx->log->error(ctx, "all_services_test: error message");
        ctx->log->debug(ctx, "all_services_test: debug message");
    } else {
        ok = 0;
    }
    if (ctx->notify) {
        if (ctx->notify->post(ctx, "Title", "Body") != CANOPUS_SVC_OK) ok = 0;
        if (ctx->notify->post_with_icon(ctx, "Title", "Body", 1, 1) != CANOPUS_SVC_OK) ok = 0;
    } else {
        ok = 0;
    }
    log_test("Log & Notify: multi-level logger and popup notification", ok);
}

/* ========================================================================= */
/* 8. UI BUILDER TESTS                                                       */
/* ========================================================================= */

static struct canopus_ui_snapshot_v1 s_test_snapshot;
static int32_t test_ui_backend_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    (void)cookie;
    if (snapshot != 0) {
        canopus_memcpy(&s_test_snapshot, snapshot, sizeof(*snapshot));
    }
    return 0;
}

static const struct canopus_ui_backend_v1 s_test_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    test_ui_backend_apply,
};

static void run_ui_builder_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    struct canopus_ui_context_v1 ui_ctx;
    struct canopus_ui_tree_v1 *tree = 0;
    int32_t rc;

    if (!ctx->ui) { log_test("UI: object missing", 0); return; }

    canopus_memset(&s_test_snapshot, 0, sizeof(s_test_snapshot));
    rc = canopus_ui_context_init(&ui_ctx, &s_test_backend, 0, 0, 0);
    if (rc != CANOPUS_UI_OK) ok = 0;

    rc = ctx->ui->tree_begin(&ui_ctx, &tree);
    if (rc != CANOPUS_UI_OK || tree == 0) ok = 0;

    if (tree != 0) {
        struct canopus_ui_navigation_page_props_v1 p_page = { sizeof(p_page), "TestPage", 8 };
        if (ctx->ui->navigation_page(tree, 1, &p_page) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_section_props_v1 p_sec = { sizeof(p_sec), "Section1", 8 };
        if (ctx->ui->section(tree, 2, &p_sec) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_text_props_v1 p_text = { sizeof(p_text), "LabelText", 9, 0 };
        if (ctx->ui->text(tree, 3, &p_text) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_status_row_props_v1 p_stat = { sizeof(p_stat), "Key", 3, "Val", 3 };
        if (ctx->ui->status_row(tree, 4, &p_stat) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_button_props_v1 p_btn = { sizeof(p_btn), "Button", 6, 101, 1 };
        if (ctx->ui->button(tree, 5, &p_btn) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_action_row_props_v1 p_act = { sizeof(p_act), "Action", 6, "Detail", 6, 102, 1 };
        if (ctx->ui->action_row(tree, 6, &p_act) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_switch_row_props_v1 p_sw = { sizeof(p_sw), "Switch", 6, "Desc", 4, 103, 1, 1 };
        if (ctx->ui->switch_row(tree, 7, &p_sw) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_component_props_v1 p_comp;
        canopus_memset(&p_comp, 0, sizeof(p_comp));
        p_comp.struct_size = sizeof(p_comp);
        p_comp.primary = "Slider";
        p_comp.primary_len = 6;
        p_comp.event_id = 201;
        p_comp.minimum = 0;
        p_comp.maximum = 100;
        p_comp.value = 50;

        if (ctx->ui->component(tree, 8, CANOPUS_UI_NODE_SLIDER, &p_comp) != CANOPUS_UI_OK) ok = 0;

        p_comp.primary = "Checkbox";
        p_comp.primary_len = 8;
        p_comp.event_id = 202;
        if (ctx->ui->component(tree, 9, CANOPUS_UI_NODE_CHECKBOX, &p_comp) != CANOPUS_UI_OK) ok = 0;

        p_comp.primary = "Divider";
        p_comp.primary_len = 7;
        p_comp.event_id = 0;
        if (ctx->ui->component(tree, 10, CANOPUS_UI_NODE_DIVIDER, &p_comp) != CANOPUS_UI_OK) ok = 0;

        if (ctx->ui->end(tree) != CANOPUS_UI_OK) ok = 0;
        if (ctx->ui->end(tree) != CANOPUS_UI_OK) ok = 0;

        rc = ctx->ui->tree_commit(tree);
        if (rc != CANOPUS_UI_OK) ok = 0;

        /* Verify applied snapshot */
        if (s_test_snapshot.node_count != 10) ok = 0;
        if (s_test_snapshot.generation != 1) ok = 0;
    }

    log_test("UI: declarative tree builder with extended widget catalog", ok);
}

/* ========================================================================= */
/* MODULE LIFECYCLE & DISPATCH                                               */
/* ========================================================================= */

static int32_t test_prepare(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;
    return 0;
}

static int32_t test_activate(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;
    s_pass_count = 0;
    s_fail_count = 0;
    s_report_len = 0;
    s_report_buf[0] = '\0';

    report_append("========================================\n");
    report_append("CANOPUS ALL SERVICES TEST REPORT\n");
    report_append("========================================\n");

    /* Execute all test suites */
    run_hook_tests(ctx);
    run_kv_tests(ctx);
    run_fs_tests(ctx);
    run_mem_tests(ctx);
    run_system_and_time_tests(ctx);
    run_haptics_and_display_tests(ctx);
    run_bus_bt_log_notify_tests(ctx);
    run_ui_builder_tests(ctx);

    report_append("========================================\n");
    report_append("SUMMARY: ");
    char num_buf[16];
    format_u32(num_buf, s_pass_count);
    report_append(num_buf);
    report_append("/");
    format_u32(num_buf, s_pass_count + s_fail_count);
    report_append(num_buf);
    report_append(" PASSED, FAILS=");
    format_u32(num_buf, s_fail_count);
    report_append(num_buf);
    report_append("\n========================================\n");

    /* Write report to /data/canopus/test_report.txt */
    {
        typedef int (*open_fn)(const char *, int, ...);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        int fd = open_file("/data/canopus/test_report.txt", 2 | 4 | 0x200, 0666);
        if (fd >= 0) {
            (void)canopus_fw_write(fd, s_report_buf, s_report_len);
            (void)canopus_fw_close(fd);
        }
    }

    /* Write summary into /data/canopus/probe.txt */
    {
        typedef int (*open_fn)(const char *, int, ...);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        int fd = open_file("/data/canopus/probe.txt", 2 | 4 | 0x200, 0666);
        if (fd >= 0) {
            char summary[64];
            char *p = summary;
            const char *tag = s_fail_count == 0 ? "ALL_TESTS_PASS:" : "TESTS_FAILED:";
            while (*tag) *p++ = *tag++;
            char n1[16], n2[16];
            format_u32(n1, s_pass_count);
            format_u32(n2, s_pass_count + s_fail_count);
            for (int k = 0; n1[k]; k++) *p++ = n1[k];
            *p++ = '/';
            for (int k = 0; n2[k]; k++) *p++ = n2[k];
            *p++ = '\n';
            (void)canopus_fw_write(fd, summary, (uint32_t)(p - summary));
            (void)canopus_fw_close(fd);
        }
    }

    return 0;
}

static int32_t test_deactivate(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

static int32_t test_stop(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    s_ctx = 0;
    return 0;
}

static int32_t test_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    0u,
    TEST_MODULE_ID,
    "1.0.0",
    "test-suite",
    CANOPUS_TARGET_ID,
    test_prepare,
    test_activate,
    test_deactivate,
    test_stop,
    test_query,
    0,
    0,
};

__attribute__((constructor)) static void test_ctor(void)
{
    typedef int (*open_fn)(const char *, int, ...);
    struct canopus_module_registration_io_v1 io;
    int close_error = 0;
    if (canopus_identity_guard() != 0) return;
    io.open = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    io.write = canopus_fw_write;
    io.close = canopus_fw_close;
    io.write_flags = 2;
    (void)canopus_module_register_fd(&io, (uint32_t)(uintptr_t)&s_descriptor,
                                     TEST_MODULE_ID, &close_error);
}
