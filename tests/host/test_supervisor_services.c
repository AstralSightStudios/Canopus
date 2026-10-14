/* Host tests: Supervisor service table (ADR-CAN-006). */
#include "canopus_test.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_services.h"
#include "canopus_memory.h"
#include "canopus_runtime.h"
#include <string.h>

/* ---- in-memory file store ------------------------------------------ */

#define MAX_FILES 8
#define MAX_FDS 8
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

static const struct canopus_sup_platform_v1 platform_with_fs = {
    "xiaomi-band-10-pro-3.101.030", p_noop, p_noop, p_load, p_stage, p_remove,
    p_persist, p_restore, 0, 0, 0, 0,
    f_open, f_read, f_write, f_close, f_remove, 0, 0, p_ms,
};
static const struct canopus_sup_platform_v1 platform_without_fs = {
    "xiaomi-band-10-pro-3.101.030", p_noop, p_noop, p_load, p_stage, p_remove,
    p_persist, p_restore, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
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
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_GROUP_COUNT, 1));
    CHECK(!canopus_services_has(0, CANOPUS_SVC_LOG, 1));
    setup(&sup, &platform_without_fs);
    ctx = canopus_supervisor_context(&sup, 0);
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_FS, 1));
    CHECK(!canopus_services_has(ctx, CANOPUS_SVC_TIME, 1));
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
        char name[8] = "f0";
        name[1] = (char)('0' + i);
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

static struct test_registry supervisor_services_tests[] = {
    { "context_exposes_groups_by_platform_capability",
      context_exposes_groups_by_platform_capability_wrapper },
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
