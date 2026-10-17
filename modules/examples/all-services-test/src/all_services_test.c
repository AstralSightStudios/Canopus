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
 *   10. BLUETOOTH (is_connected, adapter_state, l2_register_rx, l2_send)
 *   11. LOG (info, warn, error, debug, write)
 *   12. NOTIFY (post, post_with_icon)
 *   13. UI (declarative tree construction with extended catalog: Page, Header, Section, Text, Status, Button, Action, Switch, Slider, Checkbox, Divider)
 */

#include "canopus_abi.h"
#include "canopus_services.h"
#include "canopus_memory.h"
#include "canopus_module_registration.h"
#include "canopus_target_config.h"
#if defined(__has_include)
#if __has_include("canopus_band11_addresses.h")
#include "canopus_band11_addresses.h"
#endif
#endif
#include "canopus_veneer.h"
#include "canopus_ui.h"

#define TEST_MODULE_ID "services_test"

static const struct canopus_context_v1 *s_ctx;
static uint32_t s_pass_count;
static uint32_t s_fail_count;
static uint32_t s_skip_count;
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

static void log_skip(const char *name)
{
    s_skip_count++;
    report_append("[SKIP] ");
    report_append(name);
    report_append("\n");
}

static void write_breadcrumb(const char *msg)
{
#if defined(CANOPUS_SUP_NUTTX_OPEN) && defined(CANOPUS_SUP_NUTTX_WRITE) && defined(CANOPUS_SUP_NUTTX_CLOSE)
    typedef int (*open_fn)(const char *, int, ...);
    typedef int32_t (*write_fn)(int, const void *, uint32_t);
    typedef int (*close_fn)(int);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    write_fn write_file = (write_fn)(uintptr_t)CANOPUS_SUP_NUTTX_WRITE;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    int fd = open_file("/data/canopus/breadcrumb.txt", 2 | 4 | 0x200, 0666);
    if (fd >= 0) {
        (void)write_file(fd, msg, canopus_strlen(msg));
        (void)close_file(fd);
    }
#else
    (void)msg;
#endif
}

/* ========================================================================= */
/* 1. HOOK ENGINE TESTS                                                      */
/* ========================================================================= */

static volatile int s_hook_inline_called = 0;
typedef int (*math_fn_t)(int, int);
static math_fn_t s_orig_math_add = 0;

__attribute__((naked, noinline, aligned(4))) static int test_math_add(
    int a __attribute__((unused)), int b __attribute__((unused)))
{
    __asm__ volatile("adds r0, r0, r1\n\t"
                     "adds r0, #7\n\t"
                     "lsls r0, r0, #1\n\t"
                     "subs r0, #23\n\t"
                     "bx lr");
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

/* Auto hook target */
static int (*s_auto_ptr_target)(int) = test_square;
static int (*s_orig_auto_ptr_target)(int) = 0;
static volatile int s_hook_auto_called = 0;

static int hook_auto_square(int x)
{
    s_hook_auto_called++;
    int orig = s_orig_auto_ptr_target(x);
    return orig + 1000;
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

    int before_val = fn_math(arg_a, arg_b);
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

    /* 1.3 Auto hook (RAM pointer slot auto-detection & restore) */
    ok = 1;
    if (s_auto_ptr_target(10) != 100) ok = 0;

    hook_id = ctx->hook->install(ctx, (uintptr_t)&s_auto_ptr_target, (uintptr_t)hook_auto_square,
                                 (uintptr_t *)&s_orig_auto_ptr_target, CANOPUS_HOOK_AUTO);
    if (hook_id < 0) ok = 0;
    if (ctx->hook->is_hooked(ctx, (uintptr_t)&s_auto_ptr_target) != 1) ok = 0;

    if (s_auto_ptr_target(10) != 1100 || s_hook_auto_called != 1) ok = 0;

    un_rc = ctx->hook->uninstall(ctx, hook_id);
    if (un_rc != CANOPUS_SVC_OK) ok = 0;
    if (ctx->hook->is_hooked(ctx, (uintptr_t)&s_auto_ptr_target) != 0) ok = 0;

    if (s_auto_ptr_target(10) != 100) ok = 0;

    log_test("Hook: Auto-deduced pointer hook & unhook slot cleanup", ok);

    /* 1.4 Negative defense tests: short function boundary check & duplicate hook */
    ok = 1;
    uintptr_t dummy_orig = 0;
    /* test_square is < 8 bytes leaf function (return x * x): inline hook must fail-closed */
    int32_t bad_hook = ctx->hook->install(ctx, (uintptr_t)test_square, (uintptr_t)hook_square,
                                         &dummy_orig, CANOPUS_HOOK_INLINE);
    if (bad_hook != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;

    /* Duplicate hook defense */
    int32_t h1 = ctx->hook->install(ctx, (uintptr_t)test_math_add, (uintptr_t)hook_math_add,
                                   (uintptr_t *)&s_orig_math_add, CANOPUS_HOOK_INLINE);
    if (h1 >= 0) {
        int32_t dup_h = ctx->hook->install(ctx, (uintptr_t)test_math_add, (uintptr_t)hook_math_add,
                                           (uintptr_t *)&s_orig_math_add, CANOPUS_HOOK_INLINE);
        if (dup_h != CANOPUS_SVC_ERR_STATE) ok = 0;
        ctx->hook->uninstall(ctx, h1);
    } else {
        ok = 0;
    }
    log_test("Hook: boundary check & duplicate hook defense", ok);

    /* 1.5 Owner-registered dispatch mapping using module-owned code and RAM. */
    ok = 1;
    static uintptr_t s_mock_driver_slot = (uintptr_t)test_square;
    uintptr_t mock_fn = (uintptr_t)test_square;

    if (!ctx->hook->register_slot_map ||
        ctx->hook->register_slot_map(ctx, mock_fn, (uintptr_t)&s_mock_driver_slot) != CANOPUS_SVC_OK) ok = 0;

    hook_id = ctx->hook->install(ctx, mock_fn,
                                 (uintptr_t)hook_square, (uintptr_t *)&s_orig_ptr_target,
                                 CANOPUS_HOOK_AUTO);
    if (hook_id < 0) ok = 0;
    if (ctx->hook->is_hooked(ctx, mock_fn) != 1) ok = 0;
    if (s_mock_driver_slot != (uintptr_t)hook_square) ok = 0;
    if ((uintptr_t)s_orig_ptr_target != (uintptr_t)test_square) ok = 0;
    if (((int (*)(int))s_mock_driver_slot)(5) != 525) ok = 0;

    un_rc = ctx->hook->uninstall(ctx, hook_id);
    if (un_rc != CANOPUS_SVC_OK) ok = 0;
    if (ctx->hook->is_hooked(ctx, mock_fn) != 0) ok = 0;
    if (s_mock_driver_slot != (uintptr_t)test_square) ok = 0;

    log_test("Hook: registered function redirects mock RAM slot", ok);
}

/* ========================================================================= */
/* 2. KV STORE TESTS                                                         */
/* ========================================================================= */

static void run_kv_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    char sbuf[32];

    if (!ctx->kv) { log_test("KV: object missing", 0); return; }
    int32_t previous_boot = ctx->kv->get_int(ctx, "boot_probe", 0);
    if (previous_boot != 0 && previous_boot != 1) ok = 0;
    if (previous_boot == 1 && ctx->fs &&
        ctx->fs->write_all(ctx, "kv_reboot.txt", "KV_REBOOT_PASS\n", 15u) != 15) ok = 0;

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
    if (ctx->kv->set_int(ctx, "boot_probe", 1) != CANOPUS_SVC_OK) ok = 0;

    log_test("KV: roundtrip, clear & persistent reboot probe", ok);
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

    /* Open, write, sync, size, seek, tell, read, close */
    fd = ctx->fs->open(ctx, "fstest.bin", CANOPUS_FS_WRITE | CANOPUS_FS_CREATE | CANOPUS_FS_TRUNC);
    if (fd < 0) ok = 0;
    if (ctx->fs->write(ctx, fd, "HELLOWORLD", 10) != 10) ok = 0;
    {
        /* No fsync entry point is recovered yet; the service must say so. */
        int32_t sync_rc = ctx->fs->sync(ctx, fd);
        if (sync_rc != CANOPUS_SVC_OK && sync_rc != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;
    }
    if (ctx->fs->size(ctx, fd) != 10) ok = 0;
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

    log_test("FS: open, write, sync, size, seek, tell, read, close", ok);

    /* Helpers: write_all, read_all, stat, exists, rename, mkdir, remove */
    ok = 1;
    if (ctx->fs->write_all(ctx, "quick.bin", "FASTDATA", 8) != 8) ok = 0;
    if (ctx->fs->exists(ctx, "quick.bin") != 1) ok = 0;
    canopus_memset(&st, 0, sizeof(st));
    if (ctx->fs->stat(ctx, "quick.bin", &st) != 0 || st.size != 8) ok = 0;
    canopus_memset(buf, 0, sizeof(buf));
    if (ctx->fs->read_all(ctx, "quick.bin", buf, sizeof(buf)) != 8) ok = 0;
    if (canopus_strcmp(buf, "FASTDATA") != 0) ok = 0;

    /* rename */
    if (ctx->fs->rename(ctx, "quick.bin", "renamed.bin") != CANOPUS_SVC_OK) ok = 0;
    if (ctx->fs->exists(ctx, "quick.bin") != 0) ok = 0;
    if (ctx->fs->exists(ctx, "renamed.bin") != 1) ok = 0;
    if (ctx->fs->remove(ctx, "renamed.bin") != 0) ok = 0;
    if (ctx->fs->exists(ctx, "renamed.bin") != 0) ok = 0;

    /* mkdir: a flat name is a directory below the module's private root */
    int32_t mk_rc = ctx->fs->mkdir(ctx, "testsub");
    if (mk_rc != CANOPUS_SVC_OK && mk_rc != CANOPUS_SVC_ERR_EXIST) ok = 0;
    canopus_memset(&st, 0, sizeof(st));
    if (ctx->fs->stat(ctx, "testsub", &st) != 0 || st.is_dir != 1u) ok = 0;
    if (ctx->fs->write_all(ctx, "testsub/inner.bin", "IN", 2) != 2) ok = 0;
    if (ctx->fs->remove(ctx, "testsub/inner.bin") != 0) ok = 0;

    log_test("FS: write_all, read_all, stat, exists, rename, mkdir, remove", ok);

    /* Absolute paths, append mode, the missing-file code and protected state */
    ok = 1;
    if (ctx->fs->open(ctx, "absent.cfg", CANOPUS_FS_READ) != CANOPUS_SVC_ERR_NOTFOUND) ok = 0;
    if (ctx->fs->open(ctx, "/data/canopus/registry.bin", CANOPUS_FS_READ) != CANOPUS_SVC_ERR_ACCESS) ok = 0;
    if (ctx->fs->open(ctx, "/data/x/../canopus/registry.bin", CANOPUS_FS_WRITE) != CANOPUS_SVC_ERR_ACCESS) ok = 0;
    if (ctx->fs->write_all(ctx, "/data/offlinelog/canopus_services_probe.txt", "ab", 2) != 2) ok = 0;
    fd = ctx->fs->open(ctx, "/data/offlinelog/canopus_services_probe.txt", CANOPUS_FS_WRITE | CANOPUS_FS_APPEND);
    if (fd < 0 || ctx->fs->write(ctx, fd, "cd", 2) != 2) ok = 0;
    if (fd >= 0) ctx->fs->close(ctx, fd);
    canopus_memset(buf, 0, sizeof(buf));
    if (ctx->fs->read_all(ctx, "/data/offlinelog/canopus_services_probe.txt", buf, sizeof(buf)) != 4 ||
        canopus_strcmp(buf, "abcd") != 0) ok = 0;
    if (ctx->fs->remove(ctx, "/data/offlinelog/canopus_services_probe.txt") != 0) ok = 0;
    log_test("FS: absolute path, append, protected Canopus state & NOTFOUND code", ok);

    /* Directory iteration */
    ok = 1;
    void *dir = ctx->fs->opendir(ctx, ".");
    if (dir != 0) {
        struct canopus_fs_dirent de;
        int32_t dir_rc = ctx->fs->readdir(ctx, dir, &de);
        if (dir_rc != 0 && dir_rc != 1) ok = 0; /* entry or end of directory */
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

    /* Overflow realloc attempt must return NULL without crashing */
    void *bad_realloc = ctx->mem->realloc(ctx, p1, 0xFFFFFFFFu);
    if (bad_realloc != 0) ok = 0;

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

    log_test("Mem: alloc 256B, realloc 512B, overflow reject, calloc 64B & free (128K heap)", ok);
}

/* ========================================================================= */
/* 5. SYSTEM & TIME TESTS                                                    */
/* ========================================================================= */

static volatile int s_timer_test_fired = 0;
static void test_timer_cb(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data)
{
    (void)ctx;
    (void)timer_id;
    (void)user_data;
    s_timer_test_fired++;
}

static void run_system_and_time_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    if (!ctx->system) { log_test("System: object missing", 0); return; }

    const char *target = ctx->system->target_id(ctx);
    const char *module = ctx->system->module_id(ctx);
    const char *model = ctx->system->device_model(ctx);
    const char *ver = ctx->system->firmware_version(ctx);
    uint32_t batt = ctx->system->battery_level(ctx);
    uint64_t uptime = ctx->system->uptime_ms(ctx);
    int is_chg = ctx->system->is_charging(ctx);

    if (!target || !model || !ver) ok = 0;
    if (!module || canopus_strcmp(module, TEST_MODULE_ID) != 0) ok = 0;
    int is_b11 = (target && target[12] == '1' && target[13] == '1');
    if (is_b11) {
        if (canopus_strcmp(model, "Xiaomi Band 11") != 0) ok = 0;
    } else {
        if (canopus_strcmp(model, "Xiaomi Band 10 Pro") != 0) ok = 0;
    }
    /* Battery is real or unknown; never a placeholder. */
    if (batt > 100u && batt != CANOPUS_SYSTEM_BATTERY_UNKNOWN) ok = 0;
    if (is_chg != 0 && is_chg != 1 && is_chg >= 0) ok = 0;
    if (uptime == 0u) ok = 0;
    if (ctx->system->is_safe_mode(ctx) != 0) ok = 0;
    if (ctx->system->framework_revision(ctx) == 0) ok = 0;
    /* Verify reboot function pointer is non-null */
    if (ctx->system->reboot == 0) ok = 0;

    /* Verify get_property against the stock property store */
    char prop_buf[32];
    int32_t prc = ctx->system->get_property(ctx, "persist.screen.brightness_value",
                                           prop_buf, sizeof(prop_buf));
    if (prc != CANOPUS_SVC_ERR_NOTFOUND && prc != CANOPUS_SVC_ERR_UNSUPPORTED && prc < 1) ok = 0;
    if (ctx->system->get_property(ctx, "canopus.no.such.key", prop_buf, sizeof(prop_buf)) !=
            CANOPUS_SVC_ERR_NOTFOUND &&
        prc != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;

    log_test("System: target, module, model, version, uptime, battery, property & reboot ptr", ok);

    ok = 1;
    if (!ctx->time) { log_test("Time: object missing", 0); return; }

    uint32_t t1 = ctx->time->monotonic_ms(ctx);
    uint64_t rt = ctx->time->realtime_ms(ctx);
    if (rt == 0u) ok = 0;

    ctx->time->sleep_ms(ctx, 10);
    uint32_t t2 = ctx->time->monotonic_ms(ctx);
    if ((uint32_t)(t2 - t1) < 10u) ok = 0;
    log_test("Time: clocks & sleep duration", ok);
    ok = 1;

    s_timer_test_fired = 0;
    int32_t tid = ctx->time->timer_create(ctx, 2, 1, test_timer_cb, 0);
    if (tid == CANOPUS_SVC_ERR_UNSUPPORTED) {
        log_skip("Timers: no approved platform event source");
        return;
    }
    if (tid < 0) ok = 0;
    if (ctx->time->timer_start(ctx, tid) != CANOPUS_SVC_OK) ok = 0;
    ctx->time->sleep_ms(ctx, 15);
    if (s_timer_test_fired == 0) ok = 0;
    if (ctx->time->timer_stop(ctx, tid) != CANOPUS_SVC_OK) ok = 0;
    if (ctx->time->timer_destroy(ctx, tid) != CANOPUS_SVC_OK) ok = 0;

    log_test("Timers: callback dispatch, stop & destroy", ok);
}

/* ========================================================================= */
/* 6. HAPTICS & DISPLAY TESTS                                                */
/* ========================================================================= */

static void run_haptics_and_display_tests(const struct canopus_context_v1 *ctx)
{
    int ok = 1;
    int32_t rc;
    if (!ctx->haptics) { log_test("Haptics: object missing", 0); return; }

    rc = ctx->haptics->vibrate(ctx, 20);
    if (rc == CANOPUS_SVC_ERR_UNSUPPORTED) {
        log_skip("Haptics: no vibrator entry point on this target");
    } else {
        uint16_t pat[] = {20, 30, 20};
        if (rc != CANOPUS_SVC_OK && rc != CANOPUS_SVC_ERR_STATE) ok = 0; /* STATE: vibration disabled by the user */
        if (rc == CANOPUS_SVC_OK) {
            if (ctx->haptics->vibrate_pattern(ctx, pat, 3) != CANOPUS_SVC_OK) ok = 0;
            if (ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK) != CANOPUS_SVC_OK) ok = 0;
        }
        if (ctx->haptics->cancel(ctx) != CANOPUS_SVC_OK) ok = 0;
        if (ctx->haptics->vibrate(ctx, 0) != CANOPUS_SVC_ERR_ARGUMENT) ok = 0;
        log_test("Haptics: vibrate, pattern, preset click & cancel", ok);
    }

    ok = 1;
    if (!ctx->display) { log_test("Display: object missing", 0); return; }

    {
        int screen = ctx->display->is_screen_on(ctx);
        if (screen == CANOPUS_SVC_ERR_UNSUPPORTED) {
            log_skip("Display: no screen state source on this target");
        } else {
            log_test("Display: screen state is 0 or 1", screen == 0 || screen == 1);
        }
    }

    {
        uint32_t before = ctx->display->get_brightness(ctx);
        rc = ctx->display->set_brightness(ctx, 80u);
        if (rc == CANOPUS_SVC_ERR_UNSUPPORTED) {
            log_skip("Display: no brightness session entry point on this target");
        } else {
            ok = (rc == CANOPUS_SVC_OK && ctx->display->get_brightness(ctx) == 80u &&
                  ctx->display->set_brightness(ctx, 101u) == CANOPUS_SVC_ERR_ARGUMENT);
            /* give the user's setting back; the override also ends with the module */
            if (before <= 100u) (void)ctx->display->set_brightness(ctx, before);
            log_test("Display: brightness override & readback", ok);
        }
    }

    ok = 1;
    uint32_t w = 0, h = 0;
    ctx->display->get_resolution(ctx, &w, &h);
    const char *tgt = ctx->system ? ctx->system->target_id(ctx) : "";
    int is_b11 = (tgt && tgt[12] == '1' && tgt[13] == '1');
    if (is_b11) {
        if (w != 212u || h != 520u) ok = 0;
    } else {
        if (w != 336u || h != 480u) ok = 0;
    }
    log_test("Display: resolution", ok);

    int32_t lock = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN);
    if (lock == CANOPUS_SVC_ERR_UNSUPPORTED) {
        log_skip("Display: no wake lock entry point on this target");
    } else {
        ok = lock > 0 && ctx->display->release_wake_lock(ctx, lock) == CANOPUS_SVC_OK &&
             ctx->display->release_wake_lock(ctx, lock) == CANOPUS_SVC_ERR_ARGUMENT &&
             ctx->display->acquire_wake_lock(ctx, 2u) == CANOPUS_SVC_ERR_ARGUMENT;
        log_test("Display: owned wake lock acquire/release", ok);
    }

    rc = ctx->display->wake_screen(ctx);
    if (rc == CANOPUS_SVC_ERR_UNSUPPORTED) {
        log_skip("Display: no screen-on entry point on this target");
    } else {
        log_test("Display: wake screen", rc == CANOPUS_SVC_OK);
    }
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
        /* Each value is the stack's own or explicitly unknown. */
        int link = ctx->bt->is_connected(ctx);
        if (link != 0 && link != 1 && link != CANOPUS_SVC_ERR_STATE &&
            link != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;
        uint32_t adapter = ctx->bt->adapter_state(ctx);
        if (adapter != CANOPUS_BT_ADAPTER_UNKNOWN && adapter > 8u) ok = 0;
        /* 0x43 is the control channel; module channels are 0x40..0x4F. */
        if (ctx->bt->l2_register_rx(ctx, 0x43, dummy_l2_cb, 0) != CANOPUS_SVC_ERR_ARGUMENT) ok = 0;
        int32_t reg_rc = ctx->bt->l2_register_rx(ctx, 0x44, dummy_l2_cb, 0);
        if (reg_rc != CANOPUS_SVC_OK && reg_rc != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;
        if (reg_rc == CANOPUS_SVC_OK &&
            ctx->bt->l2_register_rx(ctx, 0x44, 0, 0) != CANOPUS_SVC_OK) ok = 0;
        int32_t l2_rc = ctx->bt->l2_send(ctx, 0x43, "ping", 4);
        if (l2_rc != CANOPUS_SVC_OK && l2_rc != CANOPUS_SVC_ERR_UNSUPPORTED) ok = 0;
    } else {
        ok = 0;
    }
    log_test("Bluetooth: link, adapter state & L2 registration", ok);

    /* Log & Notify */
    ok = 1;
    if (ctx->log) {
        ctx->log->write(ctx, CANOPUS_LOG_INFO, "all_services_test: log->write direct test");
        ctx->log->info(ctx, "all_services_test: info message");
        ctx->log->warn(ctx, "all_services_test: warn message");
        ctx->log->error(ctx, "all_services_test: error message");
        ctx->log->debug(ctx, "all_services_test: debug message");
    } else {
        ok = 0;
    }
    log_test("Log: multi-level logger API calls", ok);
    if (ctx->notify) {
        int32_t post_rc = ctx->notify->post(ctx, "Title", "Body");
        /* The icon is an absolute image path read under the filesystem rules. */
        int32_t rel_rc = ctx->notify->post_with_icon(ctx, "Title", "Body", "icon.bin", 0);
        int32_t gone_rc = ctx->notify->post_with_icon(ctx, "Title", "Body", "/data/none/icon.bin", 0);
        int32_t state_rc = ctx->notify->post_with_icon(ctx, "Title", "Body", "/data/canopus/manager_icon.bin", 0);
        int32_t haptic_rc = ctx->notify->post_with_icon(ctx, "Title", "Body", 0, 1);
        if (post_rc == CANOPUS_SVC_ERR_UNSUPPORTED) {
            log_skip("Notify: platform notifications unavailable");
        } else {
            log_test("Notify: title, icon path refusals & haptic result",
                     post_rc == CANOPUS_SVC_OK && rel_rc == CANOPUS_SVC_ERR_ARGUMENT &&
                     gone_rc == CANOPUS_SVC_ERR_NOTFOUND && state_rc == CANOPUS_SVC_ERR_ACCESS &&
                     (haptic_rc == CANOPUS_SVC_OK || haptic_rc == CANOPUS_NOTIFY_HAPTIC_SKIPPED));
        }
    } else {
        log_skip("Notify: service absent");
    }
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

    uint32_t caps = ctx->ui->capabilities(&ui_ctx);
    if ((caps & (CANOPUS_UI_CAP_EXTENDED_COMPONENTS | CANOPUS_UI_CAP_VALUES)) !=
        (CANOPUS_UI_CAP_EXTENDED_COMPONENTS | CANOPUS_UI_CAP_VALUES)) ok = 0;

    /* Test tree abort */
    struct canopus_ui_tree_v1 *abort_tree = 0;
    if (ctx->ui->tree_begin(&ui_ctx, &abort_tree) == CANOPUS_UI_OK && abort_tree != 0) {
        struct canopus_ui_navigation_page_props_v1 p_ab = { sizeof(p_ab), "AbortPage", 9 };
        (void)ctx->ui->navigation_page(abort_tree, 1, &p_ab);
        ctx->ui->tree_abort(abort_tree);
    } else {
        ok = 0;
    }

    rc = ctx->ui->tree_begin(&ui_ctx, &tree);
    if (rc != CANOPUS_UI_OK || tree == 0) ok = 0;

    if (tree != 0) {
        struct canopus_ui_navigation_page_props_v1 p_page = { sizeof(p_page), "TestPage", 8 };
        if (ctx->ui->navigation_page(tree, 1, &p_page) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_navigation_header_props_v1 p_hdr;
        canopus_memset(&p_hdr, 0, sizeof(p_hdr));
        p_hdr.struct_size = sizeof(p_hdr);
        p_hdr.title = "NavHeader";
        p_hdr.title_len = 9;
        p_hdr.show_back = 1;
        p_hdr.back_event_id = 99;
        if (ctx->ui->navigation_header(tree, 2, &p_hdr) != CANOPUS_UI_OK) ok = 0;
        if (ctx->ui->end(tree) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_section_props_v1 p_sec = { sizeof(p_sec), "Section1", 8 };
        if (ctx->ui->section(tree, 3, &p_sec) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_text_props_v1 p_text = { sizeof(p_text), "LabelText", 9, 0 };
        if (ctx->ui->text(tree, 4, &p_text) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_status_row_props_v1 p_stat = { sizeof(p_stat), "Key", 3, "Val", 3 };
        if (ctx->ui->status_row(tree, 5, &p_stat) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_button_props_v1 p_btn = { sizeof(p_btn), "Button", 6, 101, 1 };
        if (ctx->ui->button(tree, 6, &p_btn) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_action_row_props_v1 p_act = { sizeof(p_act), "Action", 6, "Detail", 6, 102, 1 };
        if (ctx->ui->action_row(tree, 7, &p_act) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_switch_row_props_v1 p_sw = { sizeof(p_sw), "Switch", 6, "Desc", 4, 103, 1, 1 };
        if (ctx->ui->switch_row(tree, 8, &p_sw) != CANOPUS_UI_OK) ok = 0;

        struct canopus_ui_component_props_v1 p_comp;
        canopus_memset(&p_comp, 0, sizeof(p_comp));
        p_comp.struct_size = sizeof(p_comp);
        p_comp.primary = "Slider";
        p_comp.primary_len = 6;
        p_comp.event_id = 201;
        p_comp.minimum = 0;
        p_comp.maximum = 100;
        p_comp.value = 50;

        if (ctx->ui->component(tree, 9, CANOPUS_UI_NODE_SLIDER, &p_comp) != CANOPUS_UI_OK) ok = 0;

        p_comp.primary = "Checkbox";
        p_comp.primary_len = 8;
        p_comp.event_id = 202;
        if (ctx->ui->component(tree, 10, CANOPUS_UI_NODE_CHECKBOX, &p_comp) != CANOPUS_UI_OK) ok = 0;

        p_comp.primary = "Divider";
        p_comp.primary_len = 7;
        p_comp.event_id = 0;
        if (ctx->ui->component(tree, 11, CANOPUS_UI_NODE_DIVIDER, &p_comp) != CANOPUS_UI_OK) ok = 0;

        if (ctx->ui->end(tree) != CANOPUS_UI_OK) ok = 0;
        if (ctx->ui->end(tree) != CANOPUS_UI_OK) ok = 0;

        rc = ctx->ui->tree_commit(tree);
        if (rc != CANOPUS_UI_OK) ok = 0;

        /* Verify applied snapshot */
        if (s_test_snapshot.node_count != 11) ok = 0;
        if (s_test_snapshot.generation != 1) ok = 0;
    }

    log_test("UI: declarative builder using snapshot test backend", ok);
}

/* ========================================================================= */
/* MODULE LIFECYCLE & DISPATCH                                               */
/* ========================================================================= */

static int32_t test_prepare(const struct canopus_context_v1 *ctx)
{
    write_breadcrumb("PREPARE_ENTER\n");
    s_ctx = ctx;
    return 0;
}

static int32_t test_activate(const struct canopus_context_v1 *ctx)
{
    write_breadcrumb("ACTIVATE_ENTER\n");
    s_ctx = ctx;
    s_pass_count = 0;
    s_fail_count = 0;
    s_skip_count = 0;
    s_report_len = 0;
    s_report_buf[0] = '\0';

    report_append("========================================\n");
    report_append("CANOPUS ALL SERVICES TEST REPORT\n");
    report_append("========================================\n");

    /* Execute all test suites */
    write_breadcrumb("RUN_HOOK\n");
    run_hook_tests(ctx);
    write_breadcrumb("RUN_KV\n");
    run_kv_tests(ctx);
    write_breadcrumb("RUN_FS\n");
    run_fs_tests(ctx);
    write_breadcrumb("RUN_MEM\n");
    run_mem_tests(ctx);
    write_breadcrumb("RUN_SYS_TIME\n");
    run_system_and_time_tests(ctx);
    write_breadcrumb("RUN_HAPTICS_DISP\n");
    run_haptics_and_display_tests(ctx);
    write_breadcrumb("RUN_BUS_BT\n");
    run_bus_bt_log_notify_tests(ctx);
    write_breadcrumb("RUN_UI\n");
    run_ui_builder_tests(ctx);
    write_breadcrumb("ALL_RUN_DONE\n");

    report_append("========================================\n");
    report_append("SUMMARY: ");
    char num_buf[16];
    format_u32(num_buf, s_pass_count);
    report_append(num_buf);
    report_append("/");
    format_u32(num_buf, s_pass_count + s_fail_count + s_skip_count);
    report_append(num_buf);
    report_append(" PASSED, FAILS=");
    format_u32(num_buf, s_fail_count);
    report_append(num_buf);
    report_append(", SKIPS=");
    format_u32(num_buf, s_skip_count);
    report_append(num_buf);
    report_append("\n========================================\n");

    /* Write report via microkernel VFS (works everywhere on both Band 10 Pro and Band 11) */
    if (ctx->fs && ctx->fs->write_all) {
        ctx->fs->write_all(ctx, "test_report.txt", s_report_buf, s_report_len);
    }
#if defined(CANOPUS_SUP_NUTTX_OPEN) && defined(CANOPUS_SUP_NUTTX_WRITE) && defined(CANOPUS_SUP_NUTTX_CLOSE)
    {
        typedef int (*open_fn)(const char *, int, ...);
        typedef int32_t (*write_fn)(int, const void *, uint32_t);
        typedef int (*close_fn)(int);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        write_fn write_file = (write_fn)(uintptr_t)CANOPUS_SUP_NUTTX_WRITE;
        close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
        int fd = open_file("/data/canopus/test_report.txt", 2 | 4 | 0x200, 0666);
        if (fd >= 0) {
            (void)write_file(fd, s_report_buf, s_report_len);
            (void)close_file(fd);
        }
    }
#endif

    /* Write summary into probe.txt */
    char summary[64];
    char *sp = summary;
    const char *tag = s_fail_count == 0 ? "TESTS_COMPLETE:" : "TESTS_FAILED:";
    while (*tag) *sp++ = *tag++;
    char n1[16], n2[16];
    format_u32(n1, s_pass_count);
    format_u32(n2, s_pass_count + s_fail_count + s_skip_count);
    for (int k = 0; n1[k]; k++) *sp++ = n1[k];
    *sp++ = '/';
    for (int k = 0; n2[k]; k++) *sp++ = n2[k];
    tag = " SKIPS=";
    while (*tag) *sp++ = *tag++;
    format_u32(n1, s_skip_count);
    for (int k = 0; n1[k]; k++) *sp++ = n1[k];
    *sp++ = '\n';

    if (ctx->fs && ctx->fs->write_all) {
        ctx->fs->write_all(ctx, "probe.txt", summary, (uint32_t)(sp - summary));
    }
#if defined(CANOPUS_SUP_NUTTX_OPEN) && defined(CANOPUS_SUP_NUTTX_WRITE) && defined(CANOPUS_SUP_NUTTX_CLOSE)
    {
        typedef int (*open_fn)(const char *, int, ...);
        typedef int32_t (*write_fn)(int, const void *, uint32_t);
        typedef int (*close_fn)(int);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        write_fn write_file = (write_fn)(uintptr_t)CANOPUS_SUP_NUTTX_WRITE;
        close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
        int fd = open_file("/data/canopus/probe.txt", 2 | 4 | 0x200, 0666);
        if (fd >= 0) {
            (void)write_file(fd, summary, (uint32_t)(sp - summary));
            (void)close_file(fd);
        }
    }
#endif

    if (s_fail_count > 0) {
        return -1;
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

__attribute__((constructor)) static void test_ctor(const struct canopus_context_v1 *ctx)
{
#if defined(CANOPUS_SUP_NUTTX_OPEN)
    /* If loaded by kernel modlib (Band 10 Pro / target), register via /dev/canopus CMR1 */
    typedef int (*open_fn)(const char *, int, ...);
    struct canopus_module_registration_io_v1 io;
    int close_error = 0;
    (void)ctx;
    if (canopus_identity_guard() != 0) return;
    write_breadcrumb("CTOR_ENTER\n");
    io.open = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    io.write = canopus_fw_write;
    io.close = canopus_fw_close;
    io.write_flags = 2;
    (void)canopus_module_register_fd(&io, (uint32_t)(uintptr_t)&s_descriptor,
                                     TEST_MODULE_ID, &close_error);
#else
    /* If loaded by custom loader (Band 11 / portable), r0 contains ctx */
    if (ctx != 0 && ctx->services != 0 && ctx->services->module_register != 0) {
        s_ctx = ctx;
        (void)ctx->services->module_register(ctx, &s_descriptor);
    }
#endif
}
