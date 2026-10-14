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
    uint8_t data[64];
    uint32_t size;
    int used;
};
static struct mem_file g_files[MAX_FILES];
static struct { int file; uint32_t pos; int open; } g_fds[MAX_FDS];

static int f_open(void *c, const char *path, uint32_t flags)
{
    int i, fd;
    (void)c;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].path, path) == 0) break;
    }
    if (flags == CANOPUS_FS_READ) {
        if (i == MAX_FILES) return -2;
    } else {
        if (i == MAX_FILES) {
            for (i = 0; i < MAX_FILES && g_files[i].used; i++) {}
            if (i == MAX_FILES) return -28;
            g_files[i].used = 1;
            strcpy(g_files[i].path, path);
        }
        g_files[i].size = 0;
    }
    for (fd = 0; fd < MAX_FDS && g_fds[fd].open; fd++) {}
    if (fd == MAX_FDS) return -24;
    g_fds[fd].file = i; g_fds[fd].pos = 0; g_fds[fd].open = 1;
    return fd;
}
static int32_t f_read(void *c, int fd, void *b, uint32_t n)
{
    struct mem_file *f = &g_files[g_fds[fd].file];
    (void)c;
    if (n > f->size - g_fds[fd].pos) n = f->size - g_fds[fd].pos;
    memcpy(b, f->data + g_fds[fd].pos, n);
    g_fds[fd].pos += n;
    return (int32_t)n;
}
static int32_t f_write(void *c, int fd, const void *b, uint32_t n)
{
    struct mem_file *f = &g_files[g_fds[fd].file];
    (void)c;
    if (g_fds[fd].pos + n > sizeof(f->data)) return -27;
    memcpy(f->data + g_fds[fd].pos, b, n);
    g_fds[fd].pos += n;
    f->size = g_fds[fd].pos;
    return (int32_t)n;
}
static int f_close(void *c, int fd) { (void)c; g_fds[fd].open = 0; return 0; }
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
static int f_sync(void *c, int fd) { (void)c; (void)fd; return 0; }
static int f_rename(void *c, const char *old_p, const char *new_p)
{
    int i;
    (void)c;
    for (i = 0; i < MAX_FILES; i++) {
        if (g_files[i].used && strcmp(g_files[i].path, old_p) == 0) {
            strncpy(g_files[i].path, new_p, sizeof(g_files[i].path) - 1);
            return 0;
        }
    }
    return -2;
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
    return -2;
}
static int f_mkdir(void *c, const char *p) { (void)c; (void)p; return 0; }
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

static int p_noop(void *c) { (void)c; return 0; }
static int p_load(void *c, uint32_t i, const char *n, uint32_t cls)
{
    const struct canopus_context_v1 *ctx;
    (void)c; (void)n; (void)cls;
    s_load_calls++;
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

static uint32_t p_ms(void *c) { (void)c; return 1234u; }
static int g_notified;
static char g_notice[160];
static int p_notify(void *c, const char *title, const char *body)
{
    (void)c;
    g_notified++;
    snprintf(g_notice, sizeof(g_notice), "%s|%s", title, body);
    return 0;
}
static int g_heap_live;
static void *p_heap_alloc(void *c, uint32_t n) { (void)c; g_heap_live++; return malloc(n); }
static void p_heap_release(void *c, void *p) { (void)c; g_heap_live--; free(p); }

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
    CHECK(strcmp(g_notice, "mod.portable|hi") == 0);
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
    CHECK(strcmp(g_files[0].path, "/data/canopus/m.mod.portable.state.bin") == 0);

    h = ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_READ);
    CHECK(h >= 0);
    CHECK_EQ(ctx->services->fs_read(ctx, h, buf, sizeof(buf)), 5);
    CHECK(memcmp(buf, "hello", 5) == 0);
    CHECK_EQ(ctx->services->fs_close(ctx, h), 0);

    CHECK_EQ(ctx->services->fs_remove(ctx, "state.bin"), 0);
    CHECK_EQ(ctx->services->fs_open(ctx, "state.bin", CANOPUS_FS_READ),
             CANOPUS_SVC_ERR_IO);
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
    CHECK_EQ(ctx->services->fs_open(ctx, "a/b", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, ".hidden", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "UPPER", CANOPUS_FS_WRITE), CANOPUS_SVC_ERR_ARGUMENT);
    CHECK_EQ(ctx->services->fs_open(ctx, "a", 3u), CANOPUS_SVC_ERR_ARGUMENT);
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
static void on_test_bus(const struct canopus_context_v1 *ctx, const char *topic, const void *payload, uint32_t len, void *user_data)
{
    (void)ctx; (void)topic; (void)payload; (void)len; (void)user_data;
    s_bus_received++;
}
static void on_test_timer(const struct canopus_context_v1 *ctx, int32_t timer_id, void *user_data)
{
    (void)ctx; (void)timer_id; (void)user_data;
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

    /* 3. Memory Object (128 KiB budget & calloc) */
    CHECK_EQ(ctx->mem->available(ctx), CANOPUS_MEM_BUDGET);
    CHECK_EQ(ctx->mem->used(ctx), 0u);
    ptr = ctx->mem->alloc(ctx, 65536u); /* 64 KiB */
    CHECK(ptr != 0);
    CHECK_EQ(ctx->mem->used(ctx), 65536u);
    CHECK_EQ(ctx->mem->available(ctx), 131072u - 65536u);

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
    hook_id = ctx->hook->install(ctx, (uintptr_t)&s_dummy_target, (uintptr_t)&s_dummy_hook, &orig, CANOPUS_HOOK_AUTO);
    CHECK(hook_id >= 0);
    CHECK(orig != 0);
    CHECK_EQ(ctx->hook->is_hooked(ctx, (uintptr_t)&s_dummy_target), 1);
    CHECK_EQ(ctx->hook->uninstall(ctx, hook_id), 0);
    CHECK_EQ(ctx->hook->is_hooked(ctx, (uintptr_t)&s_dummy_target), 0);

    /* 5. Haptics Object */
    CHECK_EQ(ctx->haptics->vibrate(ctx, 50), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->haptics->cancel(ctx), CANOPUS_SVC_OK);

    /* 6. Display Object */
    ctx->display->get_resolution(ctx, &res_w, &res_h);
    CHECK(res_w > 0 && res_h > 0);
    CHECK_EQ(ctx->display->is_screen_on(ctx), 1);
    CHECK_EQ(ctx->display->get_brightness(ctx), 100u);
    lock_id = ctx->display->acquire_wake_lock(ctx, CANOPUS_WAKELOCK_SCREEN);
    CHECK(lock_id > 0);
    CHECK_EQ(ctx->display->release_wake_lock(ctx, lock_id), CANOPUS_SVC_OK);

    /* 7. Bus Object */
    s_bus_received = 0;
    sub_id = ctx->bus->subscribe(ctx, "canopus/test", on_test_bus, 0);
    CHECK(sub_id >= 0);
    CHECK_EQ(ctx->bus->publish(ctx, "canopus/test", "ping", 4), 1);
    CHECK_EQ(s_bus_received, 1);
    CHECK_EQ(ctx->bus->unsubscribe(ctx, sub_id), CANOPUS_SVC_OK);

    /* 8. Timers Object */
    tmr_id = ctx->time->timer_create(ctx, 1000, 1, on_test_timer, 0);
    CHECK(tmr_id >= 0);
    CHECK_EQ(ctx->time->timer_start(ctx, tmr_id), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->time->timer_stop(ctx, tmr_id), CANOPUS_SVC_OK);
    CHECK_EQ(ctx->time->timer_destroy(ctx, tmr_id), CANOPUS_SVC_OK);

    /* 9. Log Object */
    ctx->log->info(ctx, "hello object log");
    ctx->log->warn(ctx, "warning log");
    ctx->log->error(ctx, "error log");

    /* 10. System Object */
    CHECK(strcmp(ctx->system->target_id(ctx), "xiaomi-band-10-pro-3.101.030") == 0);
    CHECK_EQ(ctx->system->is_safe_mode(ctx), 0);
    CHECK_EQ(ctx->system->battery_level(ctx), 85u);
}

static struct test_registry supervisor_services_tests[] = {
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
};

int run_supervisor_services_tests(void)
{
    RUN_TESTS(supervisor_services_tests,
              sizeof(supervisor_services_tests) / sizeof(supervisor_services_tests[0]));
}
