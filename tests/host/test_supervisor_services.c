/* Host tests: Supervisor service table (ADR-CAN-006). */
#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_services.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- in-memory file store ------------------------------------------ */

#define MAX_FILES 32
#define MAX_FDS 32
struct mem_file {
    char path[96];
    uint8_t data[4096];
    uint32_t size;
    int used;
};
static struct mem_file g_files[MAX_FILES];
static struct { int file; uint32_t pos; int open; } g_fds[MAX_FDS];
static int g_fail_open, g_fail_write, g_fail_close, g_fail_sync, g_fail_rename;
static uint32_t g_transfer_chunk;
static uint32_t g_now;
static void (*g_timer_poll)(void *);
static void *g_timer_data;

/* Directories that exist: the stock ones plus whatever mkdir created. */
#define MAX_DIRS 16
static char g_dirs[MAX_DIRS][96];
static int g_dir_count;
static int g_mkdir_calls;
static uint32_t g_last_open_flags;

static int dir_exists(const char *path)
{
    static const char *const stock[] = {"/data/canopus", "/data/offlinelog", "/data/app"};
    int i;
    for (i = 0; i < 3; i++) if (strcmp(path, stock[i]) == 0) return 1;
    for (i = 0; i < g_dir_count; i++) if (strcmp(path, g_dirs[i]) == 0) return 1;
    return 0;
}

/* The parent of `path` must exist; flat module files live in /data/canopus. */
static int parent_exists(const char *path)
{
    char parent[96];
    const char *slash = strrchr(path, '/');
    if (slash == 0 || slash == path) return 1;
    memcpy(parent, path, (size_t)(slash - path));
    parent[slash - path] = '\0';
    return dir_exists(parent);
}

static int f_open(void *c, const char *path, uint32_t flags)
{
    int i, fd;
    int writes = (flags & CANOPUS_FS_WRITE) != 0u;
    (void)c;
    g_last_open_flags = flags;
    if (g_fail_open) return -28;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].path, path) == 0) break;
    }
    if (!writes) {
        if (i == MAX_FILES) return -2;
    } else {
        if (i == MAX_FILES) {
            if (!parent_exists(path)) return -2;
            for (i = 0; i < MAX_FILES && g_files[i].used; i++) {}
            if (i == MAX_FILES) return -28;
            g_files[i].used = 1;
            strcpy(g_files[i].path, path);
            g_files[i].size = 0;
        } else if ((flags & (CANOPUS_FS_APPEND | CANOPUS_FS_READ)) == 0u ||
                   (flags & CANOPUS_FS_TRUNC) != 0u) {
            g_files[i].size = 0;
        }
    }
    for (fd = 0; fd < MAX_FDS && g_fds[fd].open; fd++) {}
    if (fd == MAX_FDS) return -24;
    g_fds[fd].file = i; g_fds[fd].open = 1;
    g_fds[fd].pos = (flags & CANOPUS_FS_APPEND) != 0u ? g_files[i].size : 0u;
    return fd;
}
static int32_t f_read(void *c, int fd, void *b, uint32_t n)
{
    struct mem_file *f = &g_files[g_fds[fd].file];
    (void)c;
    if (n > f->size - g_fds[fd].pos) n = f->size - g_fds[fd].pos;
    if (g_transfer_chunk != 0u && n > g_transfer_chunk) n = g_transfer_chunk;
    memcpy(b, f->data + g_fds[fd].pos, n);
    g_fds[fd].pos += n;
    return (int32_t)n;
}
static int32_t f_write(void *c, int fd, const void *b, uint32_t n)
{
    struct mem_file *f = &g_files[g_fds[fd].file];
    (void)c;
    if (g_fail_write && g_fds[fd].pos >= 8u) return -28;
    if (g_transfer_chunk != 0u && n > g_transfer_chunk) n = g_transfer_chunk;
    if (g_fds[fd].pos + n > sizeof(f->data)) return -27;
    memcpy(f->data + g_fds[fd].pos, b, n);
    g_fds[fd].pos += n;
    f->size = g_fds[fd].pos;
    return (int32_t)n;
}
static int f_close(void *c, int fd) { (void)c; g_fds[fd].open = 0; return g_fail_close ? -5 : 0; }
static int f_remove(void *c, const char *path)
{
    int i;
    (void)c;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].path, path) == 0) {
            g_files[i].used = 0;
            return 0;
        }
    }
    return -2;
}

static int64_t f_seek(void *c, int fd, int64_t offset, int32_t whence)
{
    struct mem_file *f;
    int64_t target = 0;
    (void)c;
    if (fd < 0 || fd >= MAX_FDS || !g_fds[fd].open) return -1;
    f = &g_files[g_fds[fd].file];
    if (whence == CANOPUS_FS_SEEK_SET) target = offset;
    else if (whence == CANOPUS_FS_SEEK_CUR) target = (int64_t)g_fds[fd].pos + offset;
    else if (whence == CANOPUS_FS_SEEK_END) target = (int64_t)f->size + offset;
    else return -1;
    if (target < 0) return -1;
    g_fds[fd].pos = (uint32_t)target;
    return target;
}
static int f_sync(void *c, int fd) { (void)c; (void)fd; return g_fail_sync ? -5 : 0; }
static int f_rename(void *c, const char *old_p, const char *new_p)
{
    int i, from = -1, to = -1;
    (void)c;
    if (g_fail_rename) return -5;
    for (i = 0; i < MAX_FILES; i++) {
        if (!g_files[i].used) continue;
        if (strcmp(g_files[i].path, old_p) == 0) from = i;
        if (strcmp(g_files[i].path, new_p) == 0) to = i;
    }
    if (from < 0) return -2;
    if (to >= 0 && to != from) g_files[to].used = 0;
    strncpy(g_files[from].path, new_p, sizeof(g_files[from].path) - 1);
    return 0;
}
static int f_stat(void *c, const char *p, void *out_st)
{
    struct canopus_fs_stat *st = (struct canopus_fs_stat *)out_st;
    int i;
    (void)c;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].path, p) == 0) {
            st->size = g_files[i].size;
            st->is_dir = 0;
            return 0;
        }
    }
    if (dir_exists(p)) {
        st->size = 0;
        st->is_dir = 1;
        return 0;
    }
    return -2;
}
static int f_mkdir(void *c, const char *p)
{
    (void)c;
    g_mkdir_calls++;
    if (dir_exists(p)) return -17;
    if (!parent_exists(p) || g_dir_count == MAX_DIRS) return -2;
    strcpy(g_dirs[g_dir_count++], p);
    return 0;
}
static void *f_dir_open(void *c, const char *p) { (void)c; (void)p; return (void *)1; }
static int f_dir_read(void *c, void *d, void *entry)
{
    struct canopus_fs_dirent *de = (struct canopus_fs_dirent *)entry;
    static int idx = 0;
    (void)c; (void)d;
    while (idx < MAX_FILES) {
        if (g_files[idx].used) {
            strncpy(de->name, g_files[idx].path, sizeof(de->name) - 1);
            de->is_dir = 0;
            idx++;
            return 0;
        }
        idx++;
    }
    idx = 0;
    return 1;
}
static int f_dir_close(void *c, void *d) { (void)c; (void)d; return 0; }

static int s_load_calls;
static int s_load_hook_registers;
static const struct canopus_module_descriptor_v1 *s_descriptor;
static struct canopus_supervisor_v1 *s_sup;
static int32_t allocate_failed_module_resources(const struct canopus_context_v1 *ctx);

static int p_noop(void *c) { (void)c; return 0; }
static int p_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    const struct canopus_context_v1 *ctx;
    (void)c; (void)n; (void)cls;
    s_load_calls++;
    if (s_load_hook_registers == 2) {
        return allocate_failed_module_resources(canopus_supervisor_context(s_sup, i));
    }
    if (s_load_hook_registers) {
        /* what a portable module's init entry does with its r0 */
        ctx = canopus_supervisor_context(s_sup, i);
        if (!canopus_services_has(ctx, CANOPUS_SVC_MODULE, 1) ||
            ctx->services->module_register(ctx, s_descriptor) != 0) {
            return -1;
        }
    }
    return CANOPUS_STATE_READY;
}
static int p_stage(void *c, const char *t, uint32_t s) { (void)c; (void)t; (void)s; return 0; }
static int p_remove(void *c, uint32_t i) { (void)c; (void)i; return 0; }
static int p_persist(void *c, const uint8_t *d, uint32_t n) { (void)c; (void)d; (void)n; return 0; }
static int p_restore(void *c, uint8_t *d, uint32_t n) { (void)c; (void)d; (void)n; return 1; }

static uint32_t p_ms(void *c) { (void)c; return g_now; }
static int p_start_timer_pump(void *c, void (*poll)(void *), void *data)
{
    (void)c;
    g_timer_poll = poll;
    g_timer_data = data;
    return 0;
}
static void advance_time(uint32_t ms)
{
    g_now += ms;
    if (g_timer_poll != 0) g_timer_poll(g_timer_data);
}
static int g_notified;
static char g_notice[160];
static int p_notify(void *c, const char *source, const char *title, const char *body)
{
    (void)c;
    g_notified++;
    snprintf(g_notice, sizeof(g_notice), "%s|%s|%s", source, title, body);
    return 0;
}
static int g_heap_live;
static void *p_heap_alloc(void *c, uint32_t n) { (void)c; g_heap_live++; return malloc(n); }
static void p_heap_release(void *c, void *p) { (void)c; g_heap_live--; free(p); }

/* Hardware fakes: a vibrator, a screen with wake locks, a battery, properties,
 * a Bluetooth stack and an L2 transport. */
static uint32_t g_vib_ms[16];
static uint32_t g_vib_at[16];
static int g_vib_count, g_vib_cancels, g_vib_rc;
static int p_vibrate(void *c, uint32_t ms)
{
    (void)c;
    if (g_vib_rc != 0) return g_vib_rc;
    if (g_vib_count < 16) { g_vib_ms[g_vib_count] = ms; g_vib_at[g_vib_count] = g_now; }
    g_vib_count++;
    return 0;
}
static int p_vibrate_cancel(void *c) { (void)c; g_vib_cancels++; return 0; }

static int g_screen = 2;
static int g_wake_calls;
static int p_screen_state(void *c) { (void)c; return g_screen; }
static int p_wake_screen(void *c) { (void)c; g_wake_calls++; g_screen = 2; return 0; }

static int g_brightness_set = -1, g_brightness_restores;
static int p_set_brightness(void *c, uint32_t pct) { (void)c; g_brightness_set = (int)pct; return 0; }
static int p_restore_brightness(void *c) { (void)c; g_brightness_set = -1; g_brightness_restores++; return 0; }
static int p_get_brightness(void *c, uint32_t *pct) { (void)c; *pct = 40u; return 0; }

static uint32_t g_lock_depth[CANOPUS_WAKELOCK_MAX];
static int p_wakelock_acquire(void *c, uint32_t index, uint32_t flags)
{
    (void)c;
    if ((flags & CANOPUS_WAKELOCK_SCREEN) == 0u) return -38;
    g_lock_depth[index]++;
    return 0;
}
static int p_wakelock_release(void *c, uint32_t index)
{
    (void)c;
    if (g_lock_depth[index] == 0u) return -22;
    g_lock_depth[index]--;
    return 0;
}

static uint32_t g_battery_percent;
static int g_battery_charging, g_battery_rc = -16;
static int p_battery_state(void *c, uint32_t *percent, int *charging)
{
    (void)c;
    if (g_battery_rc != 0) return g_battery_rc;
    *percent = g_battery_percent;
    *charging = g_battery_charging;
    return 0;
}

static int p_get_property(void *c, const char *key, char *buf, uint32_t max)
{
    const char *value;
    size_t n;
    (void)c;
    if (strcmp(key, "persist.test") != 0) return -2;
    value = "on";
    n = strlen(value);
    if (n > max - 1u) n = max - 1u;
    memcpy(buf, value, n);
    buf[n] = '\0';
    return (int)n;
}

static uint32_t g_bt_state = 4u;
static int g_bt_link = 1;
static int p_bt_adapter_state(void *c, uint32_t *s) { (void)c; *s = g_bt_state; return 0; }
static int p_bt_is_connected(void *c) { (void)c; return g_bt_link; }
static int p_l2_send(void *c, uint8_t ch, const void *p, uint16_t n)
{
    (void)c; (void)ch; (void)p; (void)n;
    return 0;
}

static const struct canopus_sup_platform_v1 platform_with_fs = {
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .register_device = p_noop,
    .unregister_device = p_noop,
    .load_module = p_load,
    .stage_package = p_stage,
    .remove_artifact = p_remove,
    .persist = p_persist,
    .restore = p_restore,
    .file_open = f_open,
    .file_read = f_read,
    .file_write = f_write,
    .file_seek = f_seek,
    .file_sync = f_sync,
    .file_close = f_close,
    .file_remove = f_remove,
    .file_rename = f_rename,
    .file_stat = f_stat,
    .dir_mkdir = f_mkdir,
    .dir_open = f_dir_open,
    .dir_read = f_dir_read,
    .dir_close = f_dir_close,
    .monotonic_ms = p_ms,
    .heap_alloc = p_heap_alloc,
    .heap_release = p_heap_release,
    .notify = p_notify,
    .start_timer_pump = p_start_timer_pump,
};
static const struct canopus_sup_platform_v1 platform_without_fs = {
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .register_device = p_noop,
    .unregister_device = p_noop,
    .load_module = p_load,
    .stage_package = p_stage,
    .remove_artifact = p_remove,
    .persist = p_persist,
    .restore = p_restore,
};

static struct canopus_sup_platform_v1 platform_with_hw;
static void init_hw_platform(void)
{
    platform_with_hw = platform_with_fs;
    platform_with_hw.vibrate = p_vibrate;
    platform_with_hw.vibrate_cancel = p_vibrate_cancel;
    platform_with_hw.set_brightness = p_set_brightness;
    platform_with_hw.get_brightness = p_get_brightness;
    platform_with_hw.restore_brightness = p_restore_brightness;
    platform_with_hw.wake_screen = p_wake_screen;
    platform_with_hw.screen_state = p_screen_state;
    platform_with_hw.wakelock_acquire = p_wakelock_acquire;
    platform_with_hw.wakelock_release = p_wakelock_release;
    platform_with_hw.battery_state = p_battery_state;
    platform_with_hw.get_property = p_get_property;
    platform_with_hw.bt_adapter_state = p_bt_adapter_state;
    platform_with_hw.bt_is_connected = p_bt_is_connected;
    platform_with_hw.l2_send = p_l2_send;
    g_vib_count = g_vib_cancels = g_vib_rc = 0;
    g_screen = 2;
    g_wake_calls = g_brightness_restores = 0;
    g_brightness_set = -1;
    memset(g_lock_depth, 0, sizeof(g_lock_depth));
    g_battery_rc = -16;
    g_bt_state = 4u;
    g_bt_link = 1;
}

static int g_activated_with_ctx;
static const struct canopus_context_v1 *g_seen_ctx;
static int d_activate(const struct canopus_context_v1 *ctx)
{
    g_seen_ctx = ctx;
    g_activated_with_ctx = ctx != 0;
    return 0;
}
static int d_noop(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }
static int d_query(struct canopus_status_writer_v1 *w) { (void)w; return 0; }

static struct canopus_module_descriptor_v1 portable_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR,
    0u, "mod.portable", "1.0.0", "host-test", CANOPUS_PORTABLE_TARGET_ID,
    d_noop, d_activate, d_noop, d_noop, d_query, 0, 0,
};

static void setup(struct canopus_supervisor_v1 *sup,
                  const struct canopus_sup_platform_v1 *platform)
{
    memset(g_files, 0, sizeof(g_files));
    memset(g_fds, 0, sizeof(g_fds));
    g_dir_count = g_mkdir_calls = 0;
    g_last_open_flags = 0u;
    g_fail_open = g_fail_write = g_fail_close = g_fail_sync = g_fail_rename = 0;
    g_transfer_chunk = 0u;
    g_now = 1234u;
    g_timer_poll = 0;
    g_timer_data = 0;
    s_load_calls = 0;
    s_load_hook_registers = 0;
    s_descriptor = &portable_descriptor;
    canopus_supervisor_init(sup, 7, platform, 0);
    s_sup = sup;
    CHECK(canopus_supervisor_add_module(sup, CANOPUS_LIFECYCLE_REMOVABLE, 1,
                                        "mod.portable") == 0);
}

TEST(notify_is_titled_by_module_and_rate_limited)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    uint32_t i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    g_notified = 0;
    CHECK_EQ(ctx->services->notify_post(ctx, 0), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->notify_post(ctx, "hi"), CANOPUS_SVC_OK);
    CHECK(strcmp(g_notice, "mod.portable|mod.portable|hi") == 0);
    for (i = 1u; i < CANOPUS_NOTIFY_MAX_POSTS; i++) {
        CHECK_EQ(ctx->services->notify_post(ctx, "x"), CANOPUS_SVC_OK);
    }
    CHECK_EQ(ctx->services->notify_post(ctx, "x"), CANOPUS_SVC_ERR_STATE);
    CHECK_EQ(g_notified, (int)CANOPUS_NOTIFY_MAX_POSTS);
    canopus_supervisor_release_services(&sup, 0u);
    CHECK_EQ(ctx->services->notify_post(ctx, "again"), CANOPUS_SVC_OK);
}

TEST(mem_is_uncapped_owned_and_released)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    void *a, *b;
    uint32_t i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    g_heap_live = 0;
    CHECK(ctx->services->mem_alloc(ctx, 0u) == 0);
    /* No per-module block/byte cap: allocations past the retired 16 KiB
     * budget succeed; the firmware heap is the only bound. */
    a = ctx->services->mem_alloc(ctx, 32u * 1024u);
    CHECK(a != 0);
    b = ctx->services->mem_alloc(ctx, 64u);
    CHECK(b != 0);
    ctx->services->mem_free(ctx, (void *)&i); /* not owned: ignored */
    CHECK_EQ(g_heap_live, 2);
    ctx->services->mem_free(ctx, a);
    CHECK_EQ(g_heap_live, 1);
    CHECK(ctx->services->mem_alloc(ctx, 128u) != 0);
    canopus_supervisor_release_services(&sup, 0u);
    CHECK_EQ(g_heap_live, 0);
}

TEST(context_exposes_groups_by_platform_capability)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK(ctx != 0);
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_MODULE, 1));
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_LOG, 1));
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_FS, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_FS, 2));
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_TIME, 1));
    CHECK_EQ(ctx->services->time_monotonic_ms(ctx), 1234u);
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_MEM, 1));
    CHECK(canopus_services_has(ctx, CANOPUS_SVC_NOTIFY, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_GROUP_COUNT, 1));
    CHECK(!canopus_services_has(0, CANOPUS_SVC_LOG, 1));
    setup(&sup, &platform_without_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_FS, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_TIME, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_MEM, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_NOTIFY, 1));
    CHECK_EQ(ctx->abi, (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR);
}

TEST(fs_roundtrip_is_private_to_the_module_namespace)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    int32_t h;
    char buf[8] = {0};
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);

    h = ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_WRITE);
    CHECK(h >= 0);
    CHECK_EQ(ctx->services->fs_write(ctx, h, "hello", 5), 5);
    CHECK_EQ(ctx->services->fs_close(ctx, h), 0);
    CHECK(strcmp(g_files[0].path, "/data/canopus/m.mod.portable/state.bin") == 0);

    h = ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_READ);
    CHECK(h >= 0);
    CHECK_EQ(ctx->services->fs_read(ctx, h, buf, sizeof(buf)), 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK_EQ(ctx->services->fs_close(ctx, h), 0);

    CHECK_EQ(ctx->services->fs_remove(ctx, "state.bin"), 0);
    /* A missing file is NOTFOUND, distinguishable from an I/O fault. */
    CHECK_EQ(ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_READ),
             CANOPUS_SVC_ERR_NOTFOUND);
    g_fail_open = 1;
    CHECK_EQ(ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_WRITE),
             CANOPUS_SVC_ERR_NOSPACE);
    g_fail_open = 0;
}

TEST(fs_rejects_escaping_names_bad_handles_and_exhaustion)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    int32_t h[CANOPUS_FS_MAX_OPEN + 1];
    uint32_t i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK_EQ(ctx->services->fs_open(ctx, "../registry.bin", CANOPUS_FS_WRITE),
             CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "..", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "a/../../b", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "a/..", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "bad\nname", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "/", CANOPUS_FS_READ), CANOPUS_SVC_ERR_NOTFOUND); /* the root exists to the filesystem, not as a file */
    CHECK_EQ(ctx->services->fs_open(ctx, "/..", CANOPUS_FS_READ), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "a", 3u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->fs->open(ctx, "a", 0x40u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->fs->open(ctx, "a", CANOPUS_FS_APPEND), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_read(ctx, 0, g_files, 1), CANOPUS_SVC_ERR_STATE);
    CHECK_EQ(ctx->services->fs_close(ctx, 99), CANOPUS_SVC_ERR_STATE);
    for (i = 0; i < CANOPUS_FS_MAX_OPEN; i++) {
        char name[16];
        snprintf(name, sizeof(name), "f%u", (unsigned int)i);
        h[i] = ctx->services->fs_open(ctx, name, CANOPUS_FS_WRITE);
        CHECK(h[i] >= 0);
    }
    CHECK_EQ(ctx->services->fs_open(ctx, "extra", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_STATE);
    canopus_supervisor_release_services(&sup, 0);
    for (i = 0; i < MAX_FDS; i++) CHECK(!g_fds[i].open);
}

TEST(fs_group_is_unsupported_without_platform_hooks)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    setup(&sup, &platform_without_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK(ctx->services->fs_open == 0);
}

TEST(log_ring_keeps_the_newest_attributed_messages)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    char longmsg[200];
    uint32_t level, slot, i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    for (i = 0; i < 10; i++) {
        char m[8] = "m0";
        m[1] = (char)('0' + i);
        ctx->services->log_write(ctx, CANOPUS_LOG_INFO, m);
    }
    CHECK_EQ(canopus_supervisor_log_count(), 8u);
    CHECK(strcmp(canopus_supervisor_log_entry(0, &level, &slot), "m2") == 0);
    CHECK(strcmp(canopus_supervisor_log_entry(7, &level, &slot), "m9") == 0);
    CHECK_EQ(level, (uint32_t)CANOPUS_LOG_INFO);
    CHECK_EQ(slot, 0u);
    CHECK(canopus_supervisor_log_entry(8, 0, 0) == 0);
    memset(longmsg, 'x', sizeof(longmsg) - 1);
    longmsg[sizeof(longmsg) - 1] = '\0';
    ctx->services->log_write(ctx, CANOPUS_LOG_WARN, longmsg);
    CHECK_EQ(strlen(canopus_supervisor_log_entry(7, 0, 0)),
             (size_t)CANOPUS_LOG_MESSAGE_MAX);
    ctx->services->log_write(ctx, 9u, "bad level"); /* ignored */
    CHECK(strcmp(canopus_supervisor_log_entry(7, 0, 0), longmsg) != 0 ||
          strlen(canopus_supervisor_log_entry(7, 0, 0)) == CANOPUS_LOG_MESSAGE_MAX);
}

TEST(portable_module_registers_through_the_service_and_gets_a_context)
{
    struct canopus_supervisor_v1 sup;
    uint8_t cmd[CANOPUS_SUP_COMMAND_SIZE];
    uint32_t words[4];
    const struct canopus_context_v1 *ctx;
    setup(&sup, &platform_with_fs);
    sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    s_load_hook_registers = 1;
    g_activated_with_ctx = 0;
    words[0] = CANOPUS_SUP_CMD_MAGIC; words[1] = CANOPUS_SUP_CMD_ACTIVATE;
    words[2] = 0; words[3] = 0;
    memcpy(cmd, words, sizeof(cmd));
    sup.loading_slot = 0; /* the platform loader holds the slot while init runs */
    CHECK_EQ(canopus_supervisor_handle_command(&sup, cmd), CANOPUS_RESULT_COMPLETED);
    CHECK(g_activated_with_ctx);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK(g_seen_ctx == ctx);
    CHECK(sup.modules[0].descriptor == &portable_descriptor);
}

TEST(register_is_rejected_outside_the_loading_window)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    sup.loading_slot = -1;
    CHECK(ctx->services->module_register(ctx, &portable_descriptor) != 0);
    sup.loading_slot = 0;
    CHECK_EQ(ctx->services->module_register(ctx, &portable_descriptor), 0);
    /* a descriptor for another firmware is still rejected */
    {
        struct canopus_module_descriptor_v1 other = portable_descriptor;
        memset(other.target_id, 0, sizeof(other.target_id));
        strcpy((char *)other.target_id, "xiaomi-band-11-4.100.139");
        CHECK(ctx->services->module_register(ctx, &other) != 0);
    }
}

static int s_dummy_target(int x) { return x + 1; }
static int s_dummy_hook(int x) { return x + 10; }
static int s_bus_received = 0;
static int s_timer_fired = 0;
static void on_test_bus(const struct canopus_context_v1 *ctx, const char *topic, const void *payload, uint32_t len, void *user_data)
{
    (void)ctx; (void)topic; (void)payload; (void)len; (void)user_data;
    s_bus_received++;
}
static void on_test_timer(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data)
{
    (void)ctx; (void)timer_id; (void)user_data;
    s_timer_fired++;
}

TEST(service_objects_are_exposed_and_functional)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    int32_t fd;
    void *ptr;
    struct canopus_fs_stat st;
    uintptr_t orig = 0;
    int32_t hook_id;
    char kbuf[32] = {0};
    uint32_t res_w = 0, res_h = 0;
    int32_t lock_id, sub_id, tmr_id;
    char r_buf[16] = {0};

    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);

    /* Verify all Service Objects are present */
    CHECK(ctx != 0);
    CHECK(ctx->fs != 0);
    CHECK(ctx->kv != 0);
    CHECK(ctx->mem != 0);
    CHECK(ctx->time != 0);
    CHECK(ctx->hook != 0);
    CHECK(ctx->log != 0);
    CHECK(ctx->notify != 0);
    CHECK(ctx->system != 0);
    CHECK(ctx->bt != 0);
    CHECK(ctx->haptics != 0);
    CHECK(ctx->display != 0);
    CHECK(ctx->bus != 0);
    CHECK(ctx->ui != 0);

    /* 1. FS Object (detailed operations & read_all/write_all) */
    fd = ctx->fs->open(ctx, "app_data.bin", CANOPUS_FS_WRITE);
    CHECK(fd >= 0);
    CHECK_EQ(ctx->fs->write(ctx, fd, "abcdefgh", 8), 8);
    CHECK_EQ(ctx->fs->tell(ctx, fd), 8);
    CHECK_EQ(ctx->fs->seek(ctx, fd, 2, CANOPUS_FS_SEEK_SET), 2);
    CHECK_EQ(ctx->fs->tell(ctx, fd), 2);
    CHECK_EQ(ctx->fs->close(ctx, fd), 0);

    CHECK_EQ(ctx->fs->exists(ctx, "app_data.bin"), 1);
    CHECK_EQ(ctx->fs->stat(ctx, "app_data.bin", &st), 0);
    CHECK_EQ(st.size, 8u);

    CHECK_EQ(ctx->fs->rename(ctx, "app_data.bin", "renamed.bin"), 0);
    CHECK_EQ(ctx->fs->exists(ctx, "app_data.bin"), 0);
    CHECK_EQ(ctx->fs->exists(ctx, "renamed.bin"), 1);

    CHECK_EQ(ctx->fs->remove(ctx, "renamed.bin"), 0);
    CHECK_EQ(ctx->fs->exists(ctx, "renamed.bin"), 0);

    CHECK_EQ(ctx->fs->write_all(ctx, "batch.txt", "12345", 5), 5);
    CHECK_EQ(ctx->fs->read_all(ctx, "batch.txt", r_buf, sizeof(r_buf)), 5);
    CHECK(memcmp(r_buf, "12345", 5) == 0);

    /* 2. KV Preferences Object */
    CHECK_EQ(ctx->kv->set_int(ctx, "volume", 75), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->kv->get_int(ctx, "volume", 0), 75);
    CHECK_EQ(ctx->kv->set_string(ctx, "token", "abc123xyz"), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->kv->get_string(ctx, "token", kbuf, sizeof(kbuf)), 9);
    CHECK(strcmp(kbuf, "abc123xyz") == 0);
    CHECK_EQ(ctx->kv->set_bool(ctx, "enabled", 1), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->kv->get_bool(ctx, "enabled", 0), 1);
    CHECK_EQ(ctx->kv->remove(ctx, "volume"), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->kv->get_int(ctx, "volume", 10), 10);
    /* Test INT32_MIN formatting without UB */
    CHECK_EQ(ctx->kv->set_int(ctx, "min_int", (int32_t)0x80000000u), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->kv->get_int(ctx, "min_int", 0), (int32_t)0x80000000u);

    /* 3. Memory Object (128 KiB budget & calloc) */
    CHECK_EQ(ctx->mem->available(ctx), CANOPUS_MEM_BUDGET);
    CHECK_EQ(ctx->mem->used(ctx), 0u);
    ptr = ctx->mem->alloc(ctx, 65536u); /* 64 KiB */
    CHECK(ptr != 0);
    CHECK_EQ(ctx->mem->used(ctx), 65536u);
    CHECK_EQ(ctx->mem->available(ctx), 131072u - 65536u);

    /* Realloc with integer overflow size must be rejected without crashing */
    CHECK(ctx->mem->realloc(ctx, ptr, UINT32_MAX) == 0);

    /* Realloc up to 100 KiB */
    ptr = ctx->mem->realloc(ctx, ptr, 102400u);
    CHECK(ptr != 0);
    CHECK_EQ(ctx->mem->used(ctx), 102400u);
    ctx->mem->free(ctx, ptr);
    CHECK_EQ(ctx->mem->used(ctx), 0u);

    ptr = ctx->mem->calloc(ctx, 16, 4);
    CHECK(ptr != 0);
    ctx->mem->free(ctx, ptr);

    /* 4. Hook Object */
    int (*pointer_slot)(int) = s_dummy_target;
    hook_id = ctx->hook->install(ctx, (uintptr_t)&pointer_slot, (uintptr_t)&s_dummy_hook, &orig, CANOPUS_HOOK_POINTER);
    CHECK(hook_id >= 0);
    CHECK(orig == (uintptr_t)s_dummy_target);
    CHECK_EQ(pointer_slot(1), 11);
    CHECK_EQ(ctx->hook->is_hooked(ctx, (uintptr_t)&pointer_slot), 1);
    CHECK_EQ(ctx->hook->uninstall(ctx, hook_id), 0);
    CHECK_EQ(pointer_slot(1), 2);
    CHECK_EQ(ctx->hook->is_hooked(ctx, (uintptr_t)&pointer_slot), 0);

    /* 5. Haptics Object: this platform has no vibrator entry point. */
    CHECK_EQ(ctx->haptics->vibrate(ctx, 50), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->haptics->cancel(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);

    /* 6. Display Object: nothing to read, nothing faked. */
    ctx->display->get_resolution(ctx, &res_w, &res_h);
    CHECK(res_w > 0 && res_h > 0);
    CHECK_EQ(ctx->display->is_screen_on(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->display->get_brightness(ctx), CANOPUS_DISPLAY_BRIGHTNESS_UNKNOWN);
    CHECK_EQ(ctx->display->set_brightness(ctx, 50u), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->display->wake_screen(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);
    lock_id = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN);
    CHECK_EQ(lock_id, CANOPUS_SVC_ERR_UNSUPPORTED);

    /* 7. Bus Object */
    s_bus_received = 0;
    sub_id = ctx->bus->subscribe(ctx, "canopus/test", on_test_bus, 0);
    CHECK(sub_id >= 0);
    CHECK_EQ(ctx->bus->publish(ctx, "canopus/test", "ping", 4), 1);
    CHECK_EQ(s_bus_received, 1);
    CHECK_EQ(ctx->bus->unsubscribe(ctx, sub_id), CANOPUS_SVC_OK);

    /* 8. Timers Object */
    s_timer_fired = 0;
    tmr_id = ctx->time->timer_create(ctx, 1000, 1, on_test_timer, 0);
    CHECK(tmr_id >= 0);
    CHECK_EQ(ctx->time->timer_start(ctx, tmr_id), CANOPUS_SVC_OK);
    canopus_supervisor_timers_poll(&sup);
    CHECK_EQ(s_timer_fired, 0);
    CHECK_EQ(ctx->time->timer_stop(ctx, tmr_id), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->time->timer_destroy(ctx, tmr_id), CANOPUS_SVC_OK);

    /* 9. Log Object */
    ctx->log->info(ctx, "hello object log");
    ctx->log->warn(ctx, "warning log");
    ctx->log->error(ctx, "error log");

    /* 10. System Object */
    CHECK(strcmp(ctx->system->target_id(ctx), "xiaomi-band-10-pro-3.101.030") == 0);
    CHECK(strcmp(ctx->system->firmware_version(ctx), "3.101.030") == 0);
    CHECK_EQ(ctx->system->is_safe_mode(ctx), 0);
    CHECK_EQ(ctx->system->battery_level(ctx), CANOPUS_SYSTEM_BATTERY_UNKNOWN);
    CHECK_EQ(ctx->system->is_charging(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);
    {
        char prop[8];
        CHECK_EQ(ctx->system->get_property(ctx, "persist.test", prop, sizeof(prop)),
                 CANOPUS_SVC_ERR_UNSUPPORTED);
    }
    CHECK_EQ(ctx->bt->is_connected(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->bt->get_rssi(ctx), CANOPUS_BT_RSSI_UNKNOWN);
    CHECK_EQ(ctx->bt->adapter_state(ctx), CANOPUS_BT_ADAPTER_UNKNOWN);
    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x44u, 0, 0), CANOPUS_SVC_ERR_UNSUPPORTED);
}

static const struct canopus_context_v1 *reboot_services(struct canopus_supervisor_v1 *sup)
{
    g_timer_poll = 0;
    CHECK_EQ(canopus_supervisor_init(sup, 7u, &platform_with_fs, 0), 0);
    CHECK_EQ(canopus_supervisor_add_module(sup, CANOPUS_LIFECYCLE_REMOVABLE, 1u,
                                          "mod.portable"), 0);
    sup->loading_slot = 0;
    return canopus_supervisor_context(sup, 0u);
}

TEST(kv_reloads_through_native_and_service_registration)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0u);
    g_transfer_chunk = 7u;
    CHECK_EQ(ctx->kv->set_int(ctx, "saved", 42), 0);
    ctx = reboot_services(&sup);
    CHECK_EQ(canopus_supervisor_register_descriptor(&sup, "mod.portable", &portable_descriptor), 0);
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 42);
    CHECK_EQ(ctx->kv->set_int(ctx, "saved", 43), 0);
    ctx = reboot_services(&sup);
    CHECK_EQ(ctx->services->module_register(ctx, &portable_descriptor), 0);
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 43);
    ctx = reboot_services(&sup);
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 43);
}

TEST(kv_failed_transactions_preserve_cache_and_disk)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    int *failures[] = { &g_fail_open, &g_fail_write, &g_fail_sync, &g_fail_close, &g_fail_rename };
    uint32_t i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0u);
    CHECK_EQ(ctx->kv->set_int(ctx, "saved", 42), 0);
    for (i = 0u; i < sizeof(failures) / sizeof(failures[0]); i++) {
        *failures[i] = 1;
        CHECK_EQ(ctx->kv->set_int(ctx, "saved", 99), CANOPUS_SVC_ERR_IO);
        *failures[i] = 0;
        CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 42);
        ctx = reboot_services(&sup);
        CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 42);
    }
    g_fail_rename = 1;
    CHECK_EQ(ctx->kv->remove(ctx, "saved"), CANOPUS_SVC_ERR_IO);
    CHECK_EQ(ctx->kv->clear(ctx), CANOPUS_SVC_ERR_IO);
    g_fail_rename = 0;
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 42);
    ctx = reboot_services(&sup);
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), 42);
    CHECK_EQ(ctx->kv->clear(ctx), 0);
    ctx = reboot_services(&sup);
    CHECK_EQ(ctx->kv->get_int(ctx, "saved", -999), -999);
    for (i = 0u; i < MAX_FDS; i++) CHECK(!g_fds[i].open);
}

static int s_timer_depth, s_timer_max_depth;
static void on_sleeping_timer(const struct canopus_context_v1 *ctx, int32_t timer_id, void *data)
{
    (void)data;
    s_timer_depth++;
    if (s_timer_depth > s_timer_max_depth) s_timer_max_depth = s_timer_depth;
    s_timer_fired++;
    if (s_timer_depth >= 5) ctx->time->timer_stop(ctx, timer_id);
    g_now += 10u;
    ctx->time->sleep_ms(ctx, 1u);
    s_timer_depth--;
}

TEST(timers_use_platform_events_without_recursive_dispatch)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 no_pump = platform_with_fs;
    const struct canopus_context_v1 *ctx;
    int32_t id;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0u);
    CHECK_EQ(ctx->time->timer_create(ctx, UINT32_MAX, 1, on_test_timer, 0), CANOPUS_SVC_ERR_ARGUMENT);
    s_timer_fired = 0;
    id = ctx->time->timer_create(ctx, 5u, 0, on_test_timer, 0);
    CHECK(id >= 0);
    g_now = UINT32_MAX - 2u;
    CHECK_EQ(ctx->time->timer_start(ctx, id), 0);
    advance_time(4u);
    CHECK_EQ(s_timer_fired, 0);
    advance_time(1u);
    CHECK_EQ(s_timer_fired, 1);
    advance_time(5u);
    CHECK_EQ(s_timer_fired, 1);
    CHECK_EQ(ctx->time->timer_destroy(ctx, id), 0);
    s_timer_fired = s_timer_depth = s_timer_max_depth = 0;
    id = ctx->time->timer_create(ctx, 5u, 1, on_sleeping_timer, 0);
    CHECK(id >= 0);
    CHECK_EQ(ctx->time->timer_start(ctx, id), 0);
    advance_time(5u);
    CHECK_EQ(s_timer_fired, 1);
    CHECK_EQ(s_timer_max_depth, 1);
    advance_time(5u);
    CHECK_EQ(s_timer_fired, 2);
    CHECK_EQ(s_timer_max_depth, 1);
    CHECK_EQ(ctx->time->timer_destroy(ctx, id), 0);
    no_pump.start_timer_pump = 0;
    setup(&sup, &no_pump);
    ctx = canopus_supervisor_context(&sup, 0u);
    CHECK_EQ(ctx->time->timer_create(ctx, 5u, 1, on_test_timer, 0), CANOPUS_SVC_ERR_UNSUPPORTED);
}

static uintptr_t s_failed_module_pointer;
static int32_t allocate_failed_module_resources(const struct canopus_context_v1 *ctx)
{
    int32_t timer;
    uintptr_t original;
    CHECK(ctx->mem->alloc(ctx, 32u) != 0);
    timer = ctx->time->timer_create(ctx, 5u, 1, on_test_timer, 0);
    CHECK(timer >= 0);
    CHECK_EQ(ctx->time->timer_start(ctx, timer), 0);
    CHECK(ctx->bus->subscribe(ctx, "failed/module", on_test_bus, 0) >= 0);
    s_failed_module_pointer = 0x20000101u;
    CHECK(ctx->hook->install(ctx, (uintptr_t)&s_failed_module_pointer, 0x20000201u,
                            &original, CANOPUS_HOOK_POINTER) >= 0);
    return -1;
}

TEST(failed_load_and_activation_release_module_resources)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_module_descriptor_v1 descriptor = portable_descriptor;
    const struct canopus_context_v1 *ctx;
    uint32_t cmd[4] = { CANOPUS_SUP_CMD_MAGIC, CANOPUS_SUP_CMD_ACTIVATE, 0u, 0u };
    uint32_t mode;
    for (mode = 1u; mode <= 2u; mode++) {
        setup(&sup, &platform_with_fs);
        ctx = canopus_supervisor_context(&sup, 0u);
        s_descriptor = &descriptor;
        descriptor.activate = allocate_failed_module_resources;
        s_load_hook_registers = (int)mode;
        g_heap_live = s_timer_fired = s_bus_received = 0;
        sup.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
        sup.loading_slot = 0;
        CHECK_EQ(canopus_supervisor_handle_command(&sup, (const uint8_t *)cmd), CANOPUS_RESULT_FAILED);
        CHECK_EQ(sup.modules[0].state, CANOPUS_STATE_FAILED);
        CHECK_EQ(g_heap_live, 0);
        CHECK_EQ(ctx->mem->used(ctx), 0u);
        CHECK_EQ(s_failed_module_pointer, 0x20000101u);
        advance_time(100u);
        CHECK_EQ(s_timer_fired, 0);
        CHECK_EQ(ctx->bus->publish(ctx, "failed/module", "x", 1u), 0);
        CHECK_EQ(s_bus_received, 0);
    }
}


static void setup_hw(struct canopus_supervisor_v1 *sup)
{
    init_hw_platform();
    setup(sup, &platform_with_hw);
}

static const struct canopus_context_v1 *add_second_module(struct canopus_supervisor_v1 *sup)
{
    CHECK_EQ(canopus_supervisor_add_module(sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.other"), 1);
    return canopus_supervisor_context(sup, 1u);
}

TEST(fs_reaches_any_path_but_canopus_state_and_keeps_modules_apart)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx, *other;
    struct canopus_fs_stat st;
    struct canopus_sup_platform_v1 no_sync = platform_with_fs;
    static const char *const protected_paths[] = {
        "/data/canopus", "/data/canopus/", "/data/canopus/registry.bin",
        "/data/canopus/inbox/x.ko.next", "/data/canopus/kv_mod.portable.bin",
        "/data/canopus/m.mod.other/secret.txt", "/data/canopus/m.mod.portable.x/file",
        "/data/canopus/m.mod.portablex", "/data/CANOPUS/registry.bin",
        "/data/Canopus/M.mod.portable/file", "/data/x/../canopus/registry.bin",
        "/data/canopus/./registry.bin", "/data//canopus//registry.bin",
        "/dev/canopus", "/canopus/install",
    };
    char buf[16] = {0};
    int32_t h;
    uint32_t i;
    setup(&sup, &platform_with_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK_EQ(canopus_supervisor_add_module(&sup, CANOPUS_LIFECYCLE_REMOVABLE, 1, "mod.portable.x"), 1);
    other = canopus_supervisor_context(&sup, 1u);

    /* private tree: the root is created on demand, deeper directories are
     * explicit, a missing parent is NOTFOUND */
    CHECK_EQ(ctx->fs->open(ctx, "cache/a.txt", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(ctx->fs->mkdir(ctx, "cache"), CANOPUS_SVC_OK);
    CHECK(dir_exists("/data/canopus/m.mod.portable/cache"));
    CHECK_EQ(ctx->fs->mkdir(ctx, "cache"), CANOPUS_SVC_ERR_EXIST);
    CHECK_EQ(ctx->fs->write_all(ctx, "cache/a.txt", "xyz", 3), 3);
    CHECK(strcmp(g_files[0].path, "/data/canopus/m.mod.portable/cache/a.txt") == 0);
    CHECK_EQ(ctx->fs->stat(ctx, "cache", &st), CANOPUS_SVC_OK);
    CHECK_EQ(st.is_dir, 1u);
    CHECK_EQ(ctx->fs->stat(ctx, "cache/a.txt", &st), CANOPUS_SVC_OK);
    CHECK_EQ(st.size, 3u);
    CHECK_EQ(ctx->fs->stat(ctx, "cache/none", &st), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(ctx->fs->write_all(ctx, "cache//./b.txt", "q", 1), 1);
    CHECK(strcmp(g_files[1].path, "/data/canopus/m.mod.portable/cache/b.txt") == 0);

    /* ids that extend one another never share files: module "mod.portable"
     * writing "x.state" and module "mod.portable.x" writing "state" differ */
    CHECK_EQ(ctx->fs->write_all(ctx, "x.state", "A", 1), 1);
    CHECK_EQ(other->fs->write_all(other, "state", "B", 1), 1);
    CHECK(strcmp(g_files[2].path, "/data/canopus/m.mod.portable/x.state") == 0);
    CHECK(strcmp(g_files[3].path, "/data/canopus/m.mod.portable.x/state") == 0);
    CHECK_EQ(ctx->fs->read_all(ctx, "x.state", buf, sizeof(buf)), 1);
    CHECK_EQ(buf[0], 'A');

    /* any other absolute path is reachable, read or write, and normalized */
    CHECK_EQ(ctx->fs->write_all(ctx, "/data/offlinelog//run.log", "L1\n", 3), 3);
    CHECK(strcmp(g_files[4].path, "/data/offlinelog/run.log") == 0);
    CHECK_EQ(ctx->fs->open(ctx, "/data/app/theme/dark.json", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(ctx->fs->open(ctx, "/etc/none.conf", CANOPUS_FS_READ), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(ctx->fs->mkdir(ctx, "/data/app/theme"), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->write_all(ctx, "/data/app/theme/./x/../dark.json", "{}", 2), 2);
    CHECK(strcmp(g_files[5].path, "/data/app/theme/dark.json") == 0);
    CHECK_EQ(ctx->fs->exists(ctx, "/data/app/theme/dark.json"), 1);
    CHECK_EQ(ctx->fs->rename(ctx, "/data/app/theme/dark.json", "/data/app/theme/light.json"), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->remove(ctx, "/data/app/theme/light.json"), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->stat(ctx, "/data/app/theme", &st), CANOPUS_SVC_OK);
    CHECK_EQ(st.is_dir, 1u);
    h = ctx->fs->open(ctx, "/data/canopus/m.mod.portable/cache/a.txt", CANOPUS_FS_READ);
    CHECK(h >= 0); /* its own private directory by absolute path */
    CHECK_EQ(ctx->fs->close(ctx, h), CANOPUS_SVC_OK);

    /* Canopus's own state is out of reach for every operation and spelling */
    for (i = 0u; i < sizeof(protected_paths) / sizeof(protected_paths[0]); i++) {
        const char *path = protected_paths[i];
        CHECK_EQ(ctx->fs->open(ctx, path, CANOPUS_FS_READ), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->open(ctx, path, CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->remove(ctx, path), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->mkdir(ctx, path), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->stat(ctx, path, &st), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->rename(ctx, path, "moved.bin"), CANOPUS_SVC_ERR_ACCESS);
        CHECK_EQ(ctx->fs->rename(ctx, "cache/a.txt", path), CANOPUS_SVC_ERR_ACCESS);
        CHECK(ctx->fs->opendir(ctx, path) == 0);
    }
    CHECK_EQ(other->fs->open(other, "/data/canopus/m.mod.portable/cache/a.txt", CANOPUS_FS_READ),
             CANOPUS_SVC_ERR_ACCESS);
    CHECK_EQ(other->fs->open(other, "/data/canopus/m.mod.portable/cache/a.txt", CANOPUS_FS_WRITE),
             CANOPUS_SVC_ERR_ACCESS);

    /* append keeps the contents, WRITE truncates, READ|WRITE keeps them */
    CHECK_EQ(ctx->fs->write_all(ctx, "log.txt", "ab", 2), 2);
    h = ctx->fs->open(ctx, "log.txt", CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | CANOPUS_FS_CREATE);
    CHECK(h >= 0);
    CHECK_EQ(g_last_open_flags, (uint32_t)(CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | CANOPUS_FS_CREATE));
    CHECK_EQ(ctx->fs->write(ctx, h, "cd", 2), 2);
    CHECK_EQ(ctx->fs->close(ctx, h), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->read_all(ctx, "log.txt", buf, sizeof(buf)), 4);
    CHECK(memcmp(buf, "abcd", 4) == 0);
    h = ctx->fs->open(ctx, "log.txt", CANOPUS_FS_RDWR);
    CHECK(h >= 0);
    CHECK_EQ(ctx->fs->size(ctx, h), 4);
    CHECK_EQ(ctx->fs->close(ctx, h), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->write_all(ctx, "log.txt", "z", 1), 1);
    CHECK_EQ(ctx->fs->read_all(ctx, "log.txt", buf, sizeof(buf)), 1);

    /* a platform without an fsync entry point says so instead of pretending */
    h = ctx->fs->open(ctx, "log.txt", CANOPUS_FS_READ);
    CHECK_EQ(ctx->fs->sync(ctx, h), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->fs->close(ctx, h), CANOPUS_SVC_OK);
    no_sync.file_sync = 0;
    setup(&sup, &no_sync);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK_EQ(ctx->fs->write_all(ctx, "log.txt", "z", 1), 1);
    h = ctx->fs->open(ctx, "log.txt", CANOPUS_FS_READ);
    CHECK_EQ(ctx->fs->sync(ctx, h), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->fs->close(ctx, h), CANOPUS_SVC_OK);
}

TEST(notify_carries_the_caller_title_and_never_fakes_icons_or_haptics)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    char longtitle[80];
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);
    g_notified = 0;
    CHECK_EQ(ctx->notify->post(ctx, "Custom title", "body"), CANOPUS_SVC_OK);
    CHECK(strcmp(g_notice, "mod.portable|Custom title|body") == 0);
    CHECK_EQ(ctx->notify->post(ctx, "", "empty title"), CANOPUS_SVC_OK);
    CHECK(strcmp(g_notice, "mod.portable|mod.portable|empty title") == 0);
    CHECK_EQ(ctx->notify->post(ctx, 0, "no title"), CANOPUS_SVC_OK);
    CHECK(strcmp(g_notice, "mod.portable|mod.portable|no title") == 0);
    memset(longtitle, 'T', sizeof(longtitle) - 1u);
    longtitle[sizeof(longtitle) - 1u] = '\0';
    CHECK_EQ(ctx->notify->post(ctx, longtitle, "b"), CANOPUS_SVC_OK);
    CHECK_EQ(strlen(g_notice), strlen("mod.portable|") + CANOPUS_NOTIFY_TITLE_MAX + strlen("|b"));
    canopus_supervisor_release_services(&sup, 0u);

    /* icons have no stock table: refused before anything is posted or shaken */
    g_notified = 0;
    CHECK_EQ(ctx->notify->post_with_icon(ctx, "t", "b", 7u, 1), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(g_notified, 0);
    CHECK_EQ(g_vib_count, 0);
    CHECK_EQ(ctx->notify->post_with_icon(ctx, "t", "b", 0u, 1), CANOPUS_SVC_OK);
    CHECK_EQ(g_notified, 1);
    CHECK_EQ(g_vib_count, 1);
    CHECK_EQ(g_vib_ms[0], 100u);
    /* a refused vibration is reported, the notification still went out */
    g_vib_rc = -16;
    CHECK_EQ(ctx->notify->post_with_icon(ctx, "t", "b", 0u, 1), CANOPUS_NOTIFY_HAPTIC_SKIPPED);
    CHECK_EQ(g_notified, 2);
    CHECK_EQ(ctx->notify->post_with_icon(ctx, "t", "b", 0u, 0), CANOPUS_SVC_OK);
}

TEST(haptics_play_one_vibration_at_a_time_and_report_the_motor_result)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 no_pump;
    const struct canopus_context_v1 *ctx, *other;
    uint16_t pattern[] = {10u, 20u, 30u};
    uint16_t too_long[CANOPUS_HAPTIC_PATTERN_MAX + 1u] = {1u};
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);
    other = add_second_module(&sup);

    CHECK_EQ(ctx->haptics->vibrate(ctx, 0u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->haptics->vibrate(ctx, CANOPUS_HAPTIC_MAX_MS + 1u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->haptics->vibrate_preset(ctx, 99u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->haptics->vibrate_pattern(ctx, 0, 1u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->haptics->vibrate_pattern(ctx, too_long, CANOPUS_HAPTIC_PATTERN_MAX + 1u),
             CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(g_vib_count, 0);

    CHECK_EQ(ctx->haptics->vibrate(ctx, 50u), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_count, 1);
    CHECK_EQ(g_vib_ms[0], 50u);

    /* on/off pattern: pulses come from the timer pump at their offsets */
    g_vib_count = 0;
    g_now = 1000u;
    CHECK_EQ(ctx->haptics->vibrate_pattern(ctx, pattern, 3u), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_count, 1);
    CHECK_EQ(g_vib_ms[0], 10u);
    advance_time(10u);
    CHECK_EQ(g_vib_count, 1); /* the 20 ms gap */
    advance_time(19u);
    CHECK_EQ(g_vib_count, 1);
    advance_time(1u);
    CHECK_EQ(g_vib_count, 2);
    CHECK_EQ(g_vib_ms[1], 30u);
    CHECK_EQ(g_vib_at[1], 1030u);
    advance_time(50u);
    CHECK_EQ(g_vib_count, 2);

    /* a double click is two pulses */
    g_vib_count = 0;
    g_now = 2000u;
    CHECK_EQ(ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_DOUBLE_CLICK), CANOPUS_SVC_OK);
    advance_time(80u);
    CHECK_EQ(g_vib_count, 2);
    CHECK_EQ(g_vib_at[1] - g_vib_at[0], 80u);

    /* cancel stops only the caller's own vibration; a new request replaces it */
    g_vib_cancels = 0;
    CHECK_EQ(ctx->haptics->vibrate(ctx, 500u), CANOPUS_SVC_OK);
    CHECK_EQ(other->haptics->cancel(other), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_cancels, 1); /* replaced by the vibrate above (its own cancel) */
    g_vib_cancels = 0;
    CHECK_EQ(other->haptics->cancel(other), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_cancels, 0);
    CHECK_EQ(other->haptics->vibrate(other, 40u), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_cancels, 1);
    CHECK_EQ(ctx->haptics->cancel(ctx), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_cancels, 1);
    CHECK_EQ(other->haptics->cancel(other), CANOPUS_SVC_OK);
    CHECK_EQ(g_vib_cancels, 2);
    CHECK_EQ(other->haptics->vibrate(other, 400u), CANOPUS_SVC_OK);
    canopus_supervisor_release_services(&sup, 1u);
    CHECK_EQ(g_vib_cancels, 3); /* unloading stops its vibration */

    /* the motor's refusal comes back and leaves nothing running */
    g_vib_rc = -16;
    CHECK_EQ(ctx->haptics->vibrate(ctx, 50u), CANOPUS_SVC_ERR_BUSY);
    g_vib_rc = -5;
    CHECK_EQ(ctx->haptics->vibrate(ctx, 50u), CANOPUS_SVC_ERR_IO);
    g_vib_rc = 0;

    /* multi-step patterns need the pump; a single pulse does not */
    no_pump = platform_with_hw;
    no_pump.start_timer_pump = 0;
    setup(&sup, &no_pump);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK_EQ(ctx->haptics->vibrate_pattern(ctx, pattern, 3u), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->haptics->vibrate(ctx, 20u), CANOPUS_SVC_OK);
}

TEST(display_reports_screen_state_holds_owned_wake_locks_and_restores_brightness)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx, *other;
    int32_t ids[CANOPUS_WAKELOCK_PER_MODULE], extra, stale;
    uint32_t i;
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);
    other = add_second_module(&sup);

    g_screen = 2;
    CHECK_EQ(ctx->display->is_screen_on(ctx), 1);
    g_screen = 1; /* always-on display is not "on" */
    CHECK_EQ(ctx->display->is_screen_on(ctx), 0);
    g_screen = 0;
    CHECK_EQ(ctx->display->is_screen_on(ctx), 0);
    CHECK_EQ(ctx->display->wake_screen(ctx), CANOPUS_SVC_OK);
    CHECK_EQ(g_wake_calls, 1);
    CHECK_EQ(ctx->display->is_screen_on(ctx), 1);

    /* wake locks: ids are owner-checked, bounded, and die with the module */
    CHECK_EQ(ctx->display->acquire_wake_lock(ctx, 0u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->acquire_wake_lock(ctx, 8u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_CPU), CANOPUS_SVC_ERR_UNSUPPORTED);
    for (i = 0u; i < CANOPUS_WAKELOCK_PER_MODULE; i++) {
        ids[i] = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN | CANOPUS_WAKELOCK_CPU);
        CHECK(ids[i] > 0);
    }
    CHECK_EQ(ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN), CANOPUS_SVC_ERR_BUSY);
    extra = other->display->acquire_wake_lock(other, CANOPUS_WAKELOCK_SCREEN);
    CHECK(extra > 0);
    CHECK_EQ(other->display->release_wake_lock(other, ids[0]), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->release_wake_lock(ctx, 0), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->release_wake_lock(ctx, ids[1]), CANOPUS_SVC_OK);
    stale = ids[1];
    CHECK_EQ(ctx->display->release_wake_lock(ctx, stale), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->release_wake_lock(ctx, ids[0] + 1000), CANOPUS_SVC_ERR_ARGUMENT);
    {
        int32_t again = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN);
        CHECK(again > 0 && again != stale); /* the slot is reused under a new id */
        CHECK_EQ(ctx->display->release_wake_lock(ctx, stale), CANOPUS_SVC_ERR_ARGUMENT);
    }
    {
        uint32_t held = 0u;
        for (i = 0u; i < CANOPUS_WAKELOCK_MAX; i++) held += g_lock_depth[i];
        CHECK_EQ(held, CANOPUS_WAKELOCK_PER_MODULE + 1u);
        canopus_supervisor_release_services(&sup, 0u);
        held = 0u;
        for (i = 0u; i < CANOPUS_WAKELOCK_MAX; i++) held += g_lock_depth[i];
        CHECK_EQ(held, 1u); /* only the other module's lock remains */
    }
    CHECK_EQ(other->display->release_wake_lock(other, extra), CANOPUS_SVC_OK);

    /* brightness: an override belongs to the last module that set it */
    CHECK_EQ(ctx->display->set_brightness(ctx, 101u), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->display->get_brightness(ctx), 40u); /* the stored manual setting */
    CHECK_EQ(ctx->display->set_brightness(ctx, 25u), CANOPUS_SVC_OK);
    CHECK_EQ(g_brightness_set, 25);
    CHECK_EQ(ctx->display->get_brightness(ctx), 25u);
    CHECK_EQ(other->display->set_brightness(other, 80u), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->display->get_brightness(ctx), 80u);
    canopus_supervisor_release_services(&sup, 0u);
    CHECK_EQ(g_brightness_restores, 0); /* not the owner any more */
    CHECK_EQ(g_brightness_set, 80);
    canopus_supervisor_release_services(&sup, 1u);
    CHECK_EQ(g_brightness_restores, 1);
    CHECK_EQ(ctx->display->get_brightness(ctx), 40u);
}

TEST(system_and_bluetooth_report_platform_values_or_unknown)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx;
    char value[8];
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);

    g_battery_rc = -16; /* the firmware has not reported yet */
    CHECK_EQ(ctx->system->battery_level(ctx), CANOPUS_SYSTEM_BATTERY_UNKNOWN);
    CHECK_EQ(ctx->system->is_charging(ctx), CANOPUS_SVC_ERR_STATE);
    g_battery_rc = 0;
    g_battery_percent = 77u;
    g_battery_charging = 1;
    CHECK_EQ(ctx->system->battery_level(ctx), 77u);
    CHECK_EQ(ctx->system->is_charging(ctx), 1);
    g_battery_charging = 0;
    CHECK_EQ(ctx->system->is_charging(ctx), 0);
    g_battery_percent = 120u;
    CHECK_EQ(ctx->system->battery_level(ctx), CANOPUS_SYSTEM_BATTERY_UNKNOWN);

    CHECK_EQ(ctx->system->get_property(ctx, "persist.test", value, sizeof(value)), 2);
    CHECK(strcmp(value, "on") == 0);
    CHECK_EQ(ctx->system->get_property(ctx, "persist.test", value, 2u), 1);
    CHECK(strcmp(value, "o") == 0);
    CHECK_EQ(ctx->system->get_property(ctx, "persist.none", value, sizeof(value)), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(ctx->system->get_property(ctx, "bad key", value, sizeof(value)), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->system->get_property(ctx, "persist.test", value, 0u), CANOPUS_SVC_ERR_ARGUMENT);

    g_bt_state = CANOPUS_BT_ADAPTER_ON;
    CHECK_EQ(ctx->bt->adapter_state(ctx), CANOPUS_BT_ADAPTER_ON);
    g_bt_state = CANOPUS_BT_ADAPTER_OFF;
    CHECK_EQ(ctx->bt->adapter_state(ctx), CANOPUS_BT_ADAPTER_OFF);
    g_bt_link = 1;
    CHECK_EQ(ctx->bt->is_connected(ctx), 1);
    g_bt_link = 0;
    CHECK_EQ(ctx->bt->is_connected(ctx), 0);
    g_bt_link = -38;
    CHECK_EQ(ctx->bt->is_connected(ctx), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(ctx->bt->get_rssi(ctx), CANOPUS_BT_RSSI_UNKNOWN); /* no stock source */
}

static int g_l2_rx_calls;
static uint8_t g_l2_rx_channel;
static uint16_t g_l2_rx_len;
static char g_l2_rx_data[8];
static void on_l2_rx(const struct canopus_context_v1 *ctx, uint8_t channel,
                     const void *payload, uint16_t len, void *user)
{
    (void)ctx; (void)user;
    g_l2_rx_calls++;
    g_l2_rx_channel = channel;
    g_l2_rx_len = len;
    memcpy(g_l2_rx_data, payload, len < sizeof(g_l2_rx_data) ? len : sizeof(g_l2_rx_data));
}

TEST(l2_receive_registration_is_validated_delivered_and_released)
{
    struct canopus_supervisor_v1 sup;
    const struct canopus_context_v1 *ctx, *other;
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);
    other = add_second_module(&sup);
    g_l2_rx_calls = 0;

    /* stock channels and the control channel are not for modules */
    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x01u, on_l2_rx, 0), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x43u, on_l2_rx, 0), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x50u, on_l2_rx, 0), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK(!canopus_supervisor_l2_rx_registered(0x44u));
    CHECK_EQ(canopus_supervisor_l2_rx_dispatch(&sup, 0x44u, "hi", 2u), 0);

    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x44u, on_l2_rx, 0), CANOPUS_SVC_OK);
    CHECK(canopus_supervisor_l2_rx_registered(0x44u));
    CHECK_EQ(other->bt->l2_register_rx(other, 0x44u, on_l2_rx, 0), CANOPUS_SVC_ERR_BUSY);
    CHECK_EQ(other->bt->l2_register_rx(other, 0x44u, 0, 0), CANOPUS_SVC_ERR_BUSY); /* not theirs */
    CHECK(canopus_supervisor_l2_rx_registered(0x44u));
    CHECK_EQ(canopus_supervisor_l2_rx_dispatch(&sup, 0x44u, "hi", 2u), 1);
    CHECK_EQ(g_l2_rx_calls, 1);
    CHECK_EQ(g_l2_rx_channel, 0x44u);
    CHECK_EQ(g_l2_rx_len, 2u);
    CHECK(memcmp(g_l2_rx_data, "hi", 2) == 0);
    CHECK_EQ(canopus_supervisor_l2_rx_dispatch(&sup, 0x45u, "hi", 2u), 0);

    CHECK_EQ(ctx->bt->l2_register_rx(ctx, 0x44u, 0, 0), CANOPUS_SVC_OK);
    CHECK(!canopus_supervisor_l2_rx_registered(0x44u));
    CHECK_EQ(canopus_supervisor_l2_rx_dispatch(&sup, 0x44u, "hi", 2u), 0);

    /* an unloading module leaves no callback behind */
    CHECK_EQ(other->bt->l2_register_rx(other, 0x4Fu, on_l2_rx, 0), CANOPUS_SVC_OK);
    canopus_supervisor_release_services(&sup, 1u);
    CHECK(!canopus_supervisor_l2_rx_registered(0x4Fu));
    CHECK_EQ(g_l2_rx_calls, 1);
}

static int g_event_calls;
static uint8_t g_event_payload[2];
static uint32_t g_event_len;
static char g_event_topic[32];
static void on_event(const struct canopus_context_v1 *ctx, const char *topic,
                     const void *payload, uint32_t len, void *user)
{
    (void)ctx; (void)user;
    g_event_calls++;
    g_event_len = len;
    memcpy(g_event_payload, payload, len > 2u ? 2u : len);
    snprintf(g_event_topic, sizeof(g_event_topic), "%s", topic);
}

TEST(system_events_are_published_on_change_and_cannot_be_forged)
{
    struct canopus_supervisor_v1 sup;
    struct canopus_sup_platform_v1 no_pump;
    const struct canopus_context_v1 *ctx, *other;
    int32_t sub;
    setup_hw(&sup);
    ctx = canopus_supervisor_context(&sup, 0);
    other = add_second_module(&sup);
    g_event_calls = 0;

    CHECK_EQ(other->bus->publish(other, CANOPUS_BUS_TOPIC_SCREEN, "\x02", 1u), CANOPUS_SVC_ERR_ACCESS);
    CHECK_EQ(other->bus->publish(other, CANOPUS_BUS_TOPIC_BATTERY, "\x01\x00", 2u), CANOPUS_SVC_ERR_ACCESS);
    CHECK_EQ(other->bus->publish(other, CANOPUS_BUS_TOPIC_BT, "\x04\x01", 2u), CANOPUS_SVC_ERR_ACCESS);

    g_screen = 2;
    sub = ctx->bus->subscribe(ctx, CANOPUS_BUS_TOPIC_SCREEN, on_event, 0);
    CHECK(sub >= 0);
    CHECK_EQ(g_event_calls, 0);
    advance_time(10u); /* first sample: the current state goes to the new subscriber */
    CHECK_EQ(g_event_calls, 1);
    CHECK(strcmp(g_event_topic, CANOPUS_BUS_TOPIC_SCREEN) == 0);
    CHECK_EQ(g_event_len, 1u);
    CHECK_EQ(g_event_payload[0], 2u);
    advance_time(600u); /* unchanged: silent */
    CHECK_EQ(g_event_calls, 1);
    g_screen = 0;
    advance_time(100u); /* sampled every 500 ms */
    CHECK_EQ(g_event_calls, 1);
    advance_time(500u);
    CHECK_EQ(g_event_calls, 2);
    CHECK_EQ(g_event_payload[0], 0u);

    /* battery and Bluetooth topics carry two bytes; an unsubscribed topic is not sampled */
    g_event_calls = 0;
    g_battery_rc = 0;
    g_battery_percent = 64u;
    g_battery_charging = 1;
    CHECK(ctx->bus->subscribe(ctx, CANOPUS_BUS_TOPIC_BATTERY, on_event, 0) >= 0);
    advance_time(500u);
    CHECK(strcmp(g_event_topic, CANOPUS_BUS_TOPIC_BATTERY) == 0);
    CHECK_EQ(g_event_len, 2u);
    CHECK_EQ(g_event_payload[0], 64u);
    CHECK_EQ(g_event_payload[1], 1u);
    g_event_calls = 0;
    g_battery_percent = 63u;
    advance_time(500u);
    CHECK_EQ(g_event_calls, 1);
    CHECK_EQ(g_event_payload[0], 63u);
    g_event_calls = 0;
    g_bt_link = 0;
    advance_time(500u);
    CHECK_EQ(g_event_calls, 0);
    CHECK(ctx->bus->subscribe(ctx, CANOPUS_BUS_TOPIC_BT, on_event, 0) >= 0);
    advance_time(500u);
    CHECK(strcmp(g_event_topic, CANOPUS_BUS_TOPIC_BT) == 0);
    CHECK_EQ(g_event_payload[0], CANOPUS_BT_ADAPTER_ON);
    CHECK_EQ(g_event_payload[1], 0u);

    /* unsubscribing stops the sampling; a module's end drops its subscriptions */
    CHECK_EQ(ctx->bus->unsubscribe(ctx, sub), CANOPUS_SVC_OK);
    canopus_supervisor_release_services(&sup, 0u);
    g_event_calls = 0;
    g_screen = 1;
    advance_time(500u);
    CHECK_EQ(g_event_calls, 0);

    /* a system topic needs the pump; refusing beats a subscription that never fires */
    no_pump = platform_with_hw;
    no_pump.start_timer_pump = 0;
    setup(&sup, &no_pump);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK_EQ(ctx->bus->subscribe(ctx, CANOPUS_BUS_TOPIC_SCREEN, on_event, 0), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK(ctx->bus->subscribe(ctx, "my/topic", on_event, 0) >= 0);
}

static struct test_registry supervisor_services_tests[] = {
    { "kv_reloads_through_native_and_service_registration",
      kv_reloads_through_native_and_service_registration_wrapper },
    { "kv_failed_transactions_preserve_cache_and_disk",
      kv_failed_transactions_preserve_cache_and_disk_wrapper },
    { "timers_use_platform_events_without_recursive_dispatch",
      timers_use_platform_events_without_recursive_dispatch_wrapper },
    { "failed_load_and_activation_release_module_resources",
      failed_load_and_activation_release_module_resources_wrapper },
    { "service_objects_are_exposed_and_functional",
      service_objects_are_exposed_and_functional_wrapper },
    { "context_exposes_groups_by_platform_capability",
      context_exposes_groups_by_platform_capability_wrapper },
    { "notify_is_titled_by_module_and_rate_limited",
      notify_is_titled_by_module_and_rate_limited_wrapper },
    { "mem_is_uncapped_owned_and_released",
      mem_is_uncapped_owned_and_released_wrapper },
    { "fs_roundtrip_is_private_to_the_module_namespace",
      fs_roundtrip_is_private_to_the_module_namespace_wrapper },
    { "fs_rejects_escaping_names_bad_handles_and_exhaustion",
      fs_rejects_escaping_names_bad_handles_and_exhaustion_wrapper },
    { "fs_group_is_unsupported_without_platform_hooks",
      fs_group_is_unsupported_without_platform_hooks_wrapper },
    { "log_ring_keeps_the_newest_attributed_messages",
      log_ring_keeps_the_newest_attributed_messages_wrapper },
    { "portable_module_registers_through_the_service_and_gets_a_context",
      portable_module_registers_through_the_service_and_gets_a_context_wrapper },
    { "register_is_rejected_outside_the_loading_window",
      register_is_rejected_outside_the_loading_window_wrapper },
    { "fs_reaches_any_path_but_canopus_state_and_keeps_modules_apart",
      fs_reaches_any_path_but_canopus_state_and_keeps_modules_apart_wrapper },
    { "notify_carries_the_caller_title_and_never_fakes_icons_or_haptics",
      notify_carries_the_caller_title_and_never_fakes_icons_or_haptics_wrapper },
    { "haptics_play_one_vibration_at_a_time_and_report_the_motor_result",
      haptics_play_one_vibration_at_a_time_and_report_the_motor_result_wrapper },
    { "display_reports_screen_state_holds_owned_wake_locks_and_restores_brightness",
      display_reports_screen_state_holds_owned_wake_locks_and_restores_brightness_wrapper },
    { "system_and_bluetooth_report_platform_values_or_unknown",
      system_and_bluetooth_report_platform_values_or_unknown_wrapper },
    { "l2_receive_registration_is_validated_delivered_and_released",
      l2_receive_registration_is_validated_delivered_and_released_wrapper },
    { "system_events_are_published_on_change_and_cannot_be_forged",
      system_events_are_published_on_change_and_cannot_be_forged_wrapper },
};

int run_supervisor_services_tests(void)
{
    RUN_TESTS(supervisor_services_tests,
              sizeof(supervisor_services_tests) / sizeof(supervisor_services_tests[0]));
}
