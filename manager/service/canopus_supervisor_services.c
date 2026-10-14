/* canopus_supervisor_services.c — Supervisor service table (ADR-CAN-006).
 *
 * Portable and host-testable: file access goes through the platform's
 * optional file_* hooks, never through firmware addresses.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"

#define SVC_LOG_ENTRIES 8u
#define SVC_FS_PATH_MAX 96u
#define SVC_FS_ROOT "/data/canopus/m."

struct svc_log_entry {
    uint32_t level;
    uint32_t slot;
    char text[CANOPUS_LOG_MESSAGE_MAX + 1u];
};

static struct canopus_supervisor_v1 *s_sup;
static struct canopus_context_v1 s_contexts[CANOPUS_SUP_MODULE_SLOTS];
static struct canopus_services_v1 s_services;
static struct svc_log_entry s_log[SVC_LOG_ENTRIES];
static uint32_t s_log_total;
static int s_fd[CANOPUS_SUP_MODULE_SLOTS][CANOPUS_FS_MAX_OPEN];
/* One heap allocation per module block: a small header precedes the payload
 * the module sees. The header links the per-slot list and records the size,
 * so ownership checks and unload reclaim stay exact without a fixed block
 * table; the firmware heap is the only bound. */
struct svc_mem_block {
    struct svc_mem_block *next;
    uint32_t size;
};

static struct svc_mem_block *s_mem_head[CANOPUS_SUP_MODULE_SLOTS];

static int svc_slot_valid(const struct canopus_context_v1 *ctx)
{
    return s_sup != 0 && ctx != 0 &&
           ctx->struct_size == sizeof(struct canopus_context_v1) &&
           ctx->slot < CANOPUS_SUP_MODULE_SLOTS &&
           ctx == &s_contexts[ctx->slot] &&
           s_sup->modules[ctx->slot].state != 0u;
}

/* ---- MODULE ---------------------------------------------------------- */

static int32_t svc_module_register(
    const struct canopus_context_v1 *ctx,
    const struct canopus_module_descriptor_v1 *descriptor)
{
    if (!svc_slot_valid(ctx) || descriptor == 0 ||
        (int32_t)ctx->slot != s_sup->loading_slot) {
        return CANOPUS_SVC_ERR_STATE;
    }
    return canopus_supervisor_register_descriptor(
               s_sup, (const char *)s_sup->modules[ctx->slot].module_id,
               descriptor) == 0
               ? CANOPUS_SVC_OK
               : CANOPUS_SVC_ERR_ARGUMENT;
}

/* ---- LOG ------------------------------------------------------------- */

static void svc_log_write(const struct canopus_context_v1 *ctx, uint32_t level,
                          const char *message)
{
    struct svc_log_entry *entry;
    uint32_t n = 0u;
    if (!svc_slot_valid(ctx) || message == 0 || level > CANOPUS_LOG_DEBUG) return;
    entry = &s_log[s_log_total % SVC_LOG_ENTRIES];
    while (n < CANOPUS_LOG_MESSAGE_MAX && message[n] != '\0') {
        entry->text[n] = message[n];
        n++;
    }
    entry->text[n] = '\0';
    entry->level = level;
    entry->slot = ctx->slot;
    s_log_total++;
}

uint32_t canopus_supervisor_log_count(void)
{
    return s_log_total < SVC_LOG_ENTRIES ? s_log_total : SVC_LOG_ENTRIES;
}

const char *canopus_supervisor_log_entry(uint32_t index, uint32_t *level,
                                         uint32_t *slot)
{
    uint32_t count = canopus_supervisor_log_count();
    const struct svc_log_entry *entry;
    if (index >= count) return 0;
    entry = &s_log[(s_log_total - count + index) % SVC_LOG_ENTRIES];
    if (level != 0) *level = entry->level;
    if (slot != 0) *slot = entry->slot;
    return entry->text;
}

/* ---- FS -------------------------------------------------------------- */

static int svc_name_ok(const char *name)
{
    uint32_t n = 0u;
    if (name == 0) return 0;
    while (name[n] != '\0') {
        char c = name[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '-' || c == '.')) return 0;
        if (++n > CANOPUS_FS_NAME_MAX) return 0;
    }
    return n != 0u && name[0] != '.';
}

static int svc_fs_path(const struct canopus_context_v1 *ctx, const char *name,
                       char out[SVC_FS_PATH_MAX])
{
    const char *id = (const char *)s_sup->modules[ctx->slot].module_id;
    uint32_t used = 0u;
    uint32_t i;
    if (!svc_name_ok(name)) return -1;
    for (i = 0u; SVC_FS_ROOT[i] != '\0'; i++) out[used++] = SVC_FS_ROOT[i];
    for (i = 0u; i < CANOPUS_SUP_MODULE_ID_MAX && id[i] != '\0'; i++) {
        if (used + 2u >= SVC_FS_PATH_MAX) return -1;
        out[used++] = id[i];
    }
    out[used++] = '.';
    for (i = 0u; name[i] != '\0'; i++) {
        if (used + 1u >= SVC_FS_PATH_MAX) return -1;
        out[used++] = name[i];
    }
    out[used] = '\0';
    return 0;
}

static int svc_fs_ready(const struct canopus_context_v1 *ctx)
{
    const struct canopus_sup_platform_v1 *pf;
    if (!svc_slot_valid(ctx)) return 0;
    pf = s_sup->platform;
    return pf != 0 && pf->file_open != 0 && pf->file_read != 0 &&
           pf->file_write != 0 && pf->file_close != 0 && pf->file_remove != 0;
}

static int32_t svc_fs_open(const struct canopus_context_v1 *ctx,
                           const char *name, uint32_t flags)
{
    char path[SVC_FS_PATH_MAX];
    uint32_t i;
    int fd;
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if ((flags != CANOPUS_FS_READ && flags != CANOPUS_FS_WRITE) ||
        svc_fs_path(ctx, name, path) != 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    for (i = 0u; i < CANOPUS_FS_MAX_OPEN; i++) {
        if (s_fd[ctx->slot][i] == 0) break;
    }
    if (i == CANOPUS_FS_MAX_OPEN) return CANOPUS_SVC_ERR_STATE;
    fd = s_sup->platform->file_open(s_sup->platform_cookie, path, flags);
    if (fd < 0) return CANOPUS_SVC_ERR_IO;
    s_fd[ctx->slot][i] = fd + 1;
    return (int32_t)i;
}

static int svc_handle_ok(const struct canopus_context_v1 *ctx, int32_t handle)
{
    return svc_fs_ready(ctx) && handle >= 0 &&
           (uint32_t)handle < CANOPUS_FS_MAX_OPEN &&
           s_fd[ctx->slot][handle] != 0;
}

static int32_t svc_fs_read(const struct canopus_context_v1 *ctx, int32_t handle,
                           void *buffer, uint32_t size)
{
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (buffer == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    return s_sup->platform->file_read(s_sup->platform_cookie,
                                      s_fd[ctx->slot][handle] - 1, buffer, size);
}

static int32_t svc_fs_write(const struct canopus_context_v1 *ctx, int32_t handle,
                            const void *buffer, uint32_t size)
{
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (buffer == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    return s_sup->platform->file_write(s_sup->platform_cookie,
                                       s_fd[ctx->slot][handle] - 1, buffer, size);
}

static int32_t svc_fs_close(const struct canopus_context_v1 *ctx, int32_t handle)
{
    int fd;
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    fd = s_fd[ctx->slot][handle] - 1;
    s_fd[ctx->slot][handle] = 0;
    return s_sup->platform->file_close(s_sup->platform_cookie, fd) < 0
               ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

static int32_t svc_fs_remove(const struct canopus_context_v1 *ctx,
                             const char *name)
{
    char path[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (svc_fs_path(ctx, name, path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    return s_sup->platform->file_remove(s_sup->platform_cookie, path) < 0
               ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

/* ---- TIME ------------------------------------------------------------ */

static uint32_t svc_time_monotonic_ms(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return s_sup->platform->monotonic_ms(s_sup->platform_cookie);
}

/* ---- MEM ------------------------------------------------------------- */

static void *svc_mem_alloc(const struct canopus_context_v1 *ctx, uint32_t size)
{
    struct svc_mem_block *block;
    void *raw;
    if (!svc_slot_valid(ctx) || size == 0u ||
        size > UINT32_MAX - (uint32_t)sizeof(struct svc_mem_block)) return 0;
    raw = s_sup->platform->heap_alloc(s_sup->platform_cookie,
                                      sizeof(struct svc_mem_block) + size);
    if (raw == 0) return 0;
    block = (struct svc_mem_block *)raw;
    block->next = s_mem_head[ctx->slot];
    block->size = size;
    s_mem_head[ctx->slot] = block;
    return (unsigned char *)raw + sizeof(struct svc_mem_block);
}

static void svc_mem_free(const struct canopus_context_v1 *ctx, void *ptr)
{
    struct svc_mem_block **link, *block;
    if (!svc_slot_valid(ctx) || ptr == 0) return;
    for (link = &s_mem_head[ctx->slot]; *link != 0; link = &(*link)->next) {
        if ((unsigned char *)(*link) + sizeof(struct svc_mem_block) ==
            (unsigned char *)ptr) {
            block = *link;
            *link = block->next;
            s_sup->platform->heap_release(s_sup->platform_cookie, block);
            return;
        }
    }
}

/* ---- NOTIFY ---------------------------------------------------------- */

static uint32_t s_notify_posts[CANOPUS_SUP_MODULE_SLOTS];

static int32_t svc_notify_post(const struct canopus_context_v1 *ctx, const char *body)
{
    char text[CANOPUS_NOTIFY_BODY_MAX + 1u];
    uint32_t n = 0u;
    if (!svc_slot_valid(ctx) || body == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_notify_posts[ctx->slot] >= CANOPUS_NOTIFY_MAX_POSTS) return CANOPUS_SVC_ERR_STATE;
    while (n < CANOPUS_NOTIFY_BODY_MAX && body[n] != '\0') {
        text[n] = body[n];
        n++;
    }
    text[n] = '\0';
    s_notify_posts[ctx->slot]++;
    return s_sup->platform->notify(s_sup->platform_cookie,
                                   (const char *)s_sup->modules[ctx->slot].module_id,
                                   text) == 0
               ? CANOPUS_SVC_OK
               : CANOPUS_SVC_ERR_IO;
}

/* ---- table / contexts ------------------------------------------------ */

void canopus_supervisor_services_bind(struct canopus_supervisor_v1 *sup)
{
    uint32_t i;
    s_sup = sup;
    s_log_total = 0u;
    canopus_memset(s_fd, 0, sizeof(s_fd));
    canopus_memset(s_mem_head, 0, sizeof(s_mem_head));
    canopus_memset(s_notify_posts, 0, sizeof(s_notify_posts));
    canopus_memset(&s_services, 0, sizeof(s_services));
    s_services.struct_size = sizeof(s_services);
    s_services.group_version[CANOPUS_SVC_MODULE] = 1u;
    s_services.group_version[CANOPUS_SVC_LOG] = 1u;
    s_services.module_register = svc_module_register;
    s_services.log_write = svc_log_write;
    if (sup != 0 && sup->platform != 0 && sup->platform->file_open != 0 &&
        sup->platform->file_read != 0 && sup->platform->file_write != 0 &&
        sup->platform->file_close != 0 && sup->platform->file_remove != 0) {
        s_services.group_version[CANOPUS_SVC_FS] = 1u;
        s_services.fs_open = svc_fs_open;
        s_services.fs_read = svc_fs_read;
        s_services.fs_write = svc_fs_write;
        s_services.fs_close = svc_fs_close;
        s_services.fs_remove = svc_fs_remove;
    }
    if (sup != 0 && sup->platform != 0 && sup->platform->monotonic_ms != 0) {
        s_services.group_version[CANOPUS_SVC_TIME] = 1u;
        s_services.time_monotonic_ms = svc_time_monotonic_ms;
    }
    if (sup != 0 && sup->platform != 0 && sup->platform->heap_alloc != 0 &&
        sup->platform->heap_release != 0) {
        s_services.group_version[CANOPUS_SVC_MEM] = 1u;
        s_services.mem_alloc = svc_mem_alloc;
        s_services.mem_free = svc_mem_free;
    }
    if (sup != 0 && sup->platform != 0 && sup->platform->notify != 0) {
        s_services.group_version[CANOPUS_SVC_NOTIFY] = 1u;
        s_services.notify_post = svc_notify_post;
    }
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        s_contexts[i].struct_size = sizeof(struct canopus_context_v1);
        s_contexts[i].abi = (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR;
        s_contexts[i].services = &s_services;
        s_contexts[i].slot = i;
    }
}

const struct canopus_context_v1 *canopus_supervisor_context(
    struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    if (sup == 0 || sup != s_sup || slot >= CANOPUS_SUP_MODULE_SLOTS) return 0;
    return &s_contexts[slot];
}

void canopus_supervisor_release_services(struct canopus_supervisor_v1 *sup,
                                         uint32_t slot)
{
    uint32_t i;
    if (sup == 0 || sup != s_sup || slot >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->platform == 0) {
        return;
    }
    if (sup->platform->file_close != 0) {
        for (i = 0u; i < CANOPUS_FS_MAX_OPEN; i++) {
            if (s_fd[slot][i] != 0) {
                (void)sup->platform->file_close(sup->platform_cookie,
                                                s_fd[slot][i] - 1);
                s_fd[slot][i] = 0;
            }
        }
    }
    s_notify_posts[slot] = 0u;
    if (sup->platform->heap_release != 0) {
        struct svc_mem_block *block = s_mem_head[slot];
        while (block != 0) {
            struct svc_mem_block *next = block->next;
            sup->platform->heap_release(sup->platform_cookie, block);
            block = next;
        }
        s_mem_head[slot] = 0;
    }
}
