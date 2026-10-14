/* canopus_supervisor_services.c — Supervisor service table & Object system (ADR-CAN-006).
 *
 * Generic capabilities are implemented once per firmware inside the
 * Supervisor and handed to modules through `canopus_context_v1`.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "../../runtime/hook/canopus_hook.h"

#define SVC_LOG_ENTRIES 8u
#define SVC_FS_PATH_MAX CANOPUS_FS_PATH_MAX
#define SVC_FS_ROOT "/data/canopus/m."

struct svc_log_entry {
    uint32_t level;
    uint32_t slot;
    char text[CANOPUS_LOG_MESSAGE_MAX + 1u];
};

static struct canopus_supervisor_v1 *s_sup;
static struct canopus_context_v1 s_contexts[CANOPUS_SUP_MODULE_SLOTS];
static struct canopus_services_v1 s_services;

/* Service Objects */
static struct canopus_fs_obj s_fs_obj;
static struct canopus_mem_obj s_mem_obj;
static struct canopus_time_obj s_time_obj;
static struct canopus_hook_obj s_hook_obj;
static struct canopus_log_obj s_log_obj;
static struct canopus_notify_obj s_notify_obj;
static struct canopus_system_obj s_system_obj;
static struct canopus_bluetooth_obj s_bt_obj;

static struct svc_log_entry s_log[SVC_LOG_ENTRIES];
static uint32_t s_log_total;
static int s_fd[CANOPUS_SUP_MODULE_SLOTS][CANOPUS_FS_MAX_OPEN];
struct svc_mem_block {
    struct svc_mem_block *next;
    uint32_t size;
};

static struct svc_mem_block *s_mem_head[CANOPUS_SUP_MODULE_SLOTS];
static uint32_t s_notify_posts[CANOPUS_SUP_MODULE_SLOTS];

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

static void svc_log_info(const struct canopus_context_v1 *ctx, const char *message)
{
    svc_log_write(ctx, CANOPUS_LOG_INFO, message);
}

static void svc_log_warn(const struct canopus_context_v1 *ctx, const char *message)
{
    svc_log_write(ctx, CANOPUS_LOG_WARN, message);
}

static void svc_log_error(const struct canopus_context_v1 *ctx, const char *message)
{
    svc_log_write(ctx, CANOPUS_LOG_ERROR, message);
}

static void svc_log_debug(const struct canopus_context_v1 *ctx, const char *message)
{
    svc_log_write(ctx, CANOPUS_LOG_DEBUG, message);
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

/* ---- FS (Complete File System) --------------------------------------- */

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

static int svc_handle_ok(const struct canopus_context_v1 *ctx, int32_t handle)
{
    return svc_fs_ready(ctx) && handle >= 0 &&
           (uint32_t)handle < CANOPUS_FS_MAX_OPEN &&
           s_fd[ctx->slot][handle] != 0;
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

static int32_t svc_fs_open_obj(const struct canopus_context_v1 *ctx,
                               const char *name, uint32_t flags)
{
    char path[SVC_FS_PATH_MAX];
    uint32_t i;
    int fd;
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (svc_fs_path(ctx, name, path) != 0) {
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

static int64_t svc_fs_seek(const struct canopus_context_v1 *ctx, int32_t handle,
                           int64_t offset, int32_t whence)
{
    if (!svc_handle_ok(ctx, handle) || s_sup->platform->file_seek == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    return s_sup->platform->file_seek(s_sup->platform_cookie,
                                      s_fd[ctx->slot][handle] - 1, offset, whence);
}

static int64_t svc_fs_tell(const struct canopus_context_v1 *ctx, int32_t handle)
{
    return svc_fs_seek(ctx, handle, 0, CANOPUS_FS_SEEK_CUR);
}

static int64_t svc_fs_size(const struct canopus_context_v1 *ctx, int32_t handle)
{
    int64_t cur, end;
    if (!svc_handle_ok(ctx, handle) || s_sup->platform->file_seek == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    cur = s_sup->platform->file_seek(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1, 0, CANOPUS_FS_SEEK_CUR);
    end = s_sup->platform->file_seek(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1, 0, CANOPUS_FS_SEEK_END);
    (void)s_sup->platform->file_seek(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1, cur, CANOPUS_FS_SEEK_SET);
    return end;
}

static int32_t svc_fs_sync(const struct canopus_context_v1 *ctx, int32_t handle)
{
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform->file_sync != 0) {
        return s_sup->platform->file_sync(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1);
    }
    return CANOPUS_SVC_OK;
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

static int32_t svc_fs_remove(const struct canopus_context_v1 *ctx, const char *name)
{
    char path[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (svc_fs_path(ctx, name, path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    return s_sup->platform->file_remove(s_sup->platform_cookie, path) < 0
               ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

static int32_t svc_fs_rename(const struct canopus_context_v1 *ctx,
                             const char *old_path, const char *new_path)
{
    char full_old[SVC_FS_PATH_MAX];
    char full_new[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx) || s_sup->platform->file_rename == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    if (svc_fs_path(ctx, old_path, full_old) != 0 ||
        svc_fs_path(ctx, new_path, full_new) != 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    return s_sup->platform->file_rename(s_sup->platform_cookie, full_old, full_new) < 0
               ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

static int32_t svc_fs_stat(const struct canopus_context_v1 *ctx, const char *path,
                           struct canopus_fs_stat *out_stat)
{
    char full_path[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx) || out_stat == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    if (svc_fs_path(ctx, path, full_path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_sup->platform->file_stat != 0) {
        return s_sup->platform->file_stat(s_sup->platform_cookie, full_path, out_stat);
    }
    /* Fallback: try open read, size, close */
    {
        int fd = s_sup->platform->file_open(s_sup->platform_cookie, full_path, CANOPUS_FS_READ);
        if (fd < 0) return CANOPUS_SVC_ERR_NOTFOUND;
        canopus_memset(out_stat, 0, sizeof(*out_stat));
        if (s_sup->platform->file_seek != 0) {
            int64_t sz = s_sup->platform->file_seek(s_sup->platform_cookie, fd, 0, CANOPUS_FS_SEEK_END);
            out_stat->size = sz >= 0 ? (uint32_t)sz : 0u;
        }
        (void)s_sup->platform->file_close(s_sup->platform_cookie, fd);
        return CANOPUS_SVC_OK;
    }
}

static int32_t svc_fs_exists(const struct canopus_context_v1 *ctx, const char *path)
{
    struct canopus_fs_stat st;
    return svc_fs_stat(ctx, path, &st) == CANOPUS_SVC_OK ? 1 : 0;
}

static int32_t svc_fs_mkdir(const struct canopus_context_v1 *ctx, const char *path)
{
    char full_path[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_mkdir == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (svc_fs_path(ctx, path, full_path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    return s_sup->platform->dir_mkdir(s_sup->platform_cookie, full_path) < 0
               ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

static void *svc_fs_opendir(const struct canopus_context_v1 *ctx, const char *path)
{
    char full_path[SVC_FS_PATH_MAX];
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_open == 0) return 0;
    if (svc_fs_path(ctx, path, full_path) != 0) return 0;
    return s_sup->platform->dir_open(s_sup->platform_cookie, full_path);
}

static int32_t svc_fs_readdir(const struct canopus_context_v1 *ctx, void *dir,
                              struct canopus_fs_dirent *out_entry)
{
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_read == 0 || dir == 0 || out_entry == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    return s_sup->platform->dir_read(s_sup->platform_cookie, dir, out_entry);
}

static int32_t svc_fs_closedir(const struct canopus_context_v1 *ctx, void *dir)
{
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_close == 0 || dir == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    return s_sup->platform->dir_close(s_sup->platform_cookie, dir);
}

/* ---- MEM (128 KiB Budgeted Heap) ------------------------------------- */

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

static void *svc_mem_realloc(const struct canopus_context_v1 *ctx, void *ptr, uint32_t new_size)
{
    struct svc_mem_block **link, *block;
    void *new_raw;
    struct svc_mem_block *new_block;
    if (!svc_slot_valid(ctx)) return 0;
    if (ptr == 0) return svc_mem_alloc(ctx, new_size);
    if (new_size == 0u) {
        svc_mem_free(ctx, ptr);
        return 0;
    }
    for (link = &s_mem_head[ctx->slot]; *link != 0; link = &(*link)->next) {
        if ((unsigned char *)(*link) + sizeof(struct svc_mem_block) == (unsigned char *)ptr) {
            block = *link;
            new_raw = s_sup->platform->heap_alloc(s_sup->platform_cookie, sizeof(struct svc_mem_block) + new_size);
            if (new_raw == 0) return 0;
            new_block = (struct svc_mem_block *)new_raw;
            new_block->size = new_size;
            new_block->next = block->next;
            canopus_memcpy((unsigned char *)new_raw + sizeof(struct svc_mem_block), ptr, block->size < new_size ? block->size : new_size);
            *link = new_block;
            s_sup->platform->heap_release(s_sup->platform_cookie, block);
            return (unsigned char *)new_raw + sizeof(struct svc_mem_block);
        }
    }
    return 0;
}

static uint32_t svc_mem_used(const struct canopus_context_v1 *ctx)
{
    struct svc_mem_block *b;
    uint32_t total = 0u;
    if (!svc_slot_valid(ctx)) return 0u;
    for (b = s_mem_head[ctx->slot]; b != 0; b = b->next) {
        total += b->size;
    }
    return total;
}

static uint32_t svc_mem_available(const struct canopus_context_v1 *ctx)
{
    uint32_t used = svc_mem_used(ctx);
    return used >= CANOPUS_MEM_BUDGET ? 0u : (CANOPUS_MEM_BUDGET - used);
}

/* ---- TIME ------------------------------------------------------------ */

static uint32_t svc_time_monotonic_ms(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return s_sup != 0 && s_sup->platform != 0 && s_sup->platform->monotonic_ms != 0
               ? s_sup->platform->monotonic_ms(s_sup->platform_cookie) : 0u;
}

static uint64_t svc_time_realtime_ms(const struct canopus_context_v1 *ctx)
{
    return (uint64_t)svc_time_monotonic_ms(ctx);
}

static void svc_time_sleep_ms(const struct canopus_context_v1 *ctx, uint32_t ms)
{
    (void)ctx;
    (void)ms;
}

/* ---- HOOK (Universal Function Hook Engine) --------------------------- */

static int32_t svc_hook_install(const struct canopus_context_v1 *ctx,
                                uintptr_t target_fn,
                                uintptr_t hook_fn,
                                uintptr_t *orig_fn,
                                canopus_hook_type_t hook_type)
{
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    return canopus_hook_install(ctx->slot, target_fn, hook_fn, orig_fn, hook_type);
}

static int32_t svc_hook_uninstall(const struct canopus_context_v1 *ctx, int32_t hook_id)
{
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    return canopus_hook_uninstall(ctx->slot, hook_id);
}

/* ---- NOTIFY ---------------------------------------------------------- */

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
    return (s_sup->platform != 0 && s_sup->platform->notify != 0 &&
            s_sup->platform->notify(s_sup->platform_cookie,
                                    (const char *)s_sup->modules[ctx->slot].module_id,
                                    text) == 0)
               ? CANOPUS_SVC_OK
               : CANOPUS_SVC_ERR_IO;
}

static int32_t svc_notify_post_obj(const struct canopus_context_v1 *ctx,
                                   const char *title, const char *body)
{
    (void)title;
    return svc_notify_post(ctx, body);
}

/* ---- SYSTEM ---------------------------------------------------------- */

static const char *svc_system_target_id(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return (s_sup != 0 && s_sup->platform != 0) ? s_sup->platform->target_id : "";
}

static uint32_t svc_system_framework_revision(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return s_sup != 0 ? s_sup->framework_revision : 0u;
}

static int svc_system_is_safe_mode(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return s_sup != 0 ? s_sup->safe_mode : 0;
}

static int32_t svc_system_reboot(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    if (s_sup != 0 && s_sup->platform != 0 && s_sup->platform->reboot != 0) {
        return s_sup->platform->reboot(s_sup->platform_cookie);
    }
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}

/* ---- BLUETOOTH ------------------------------------------------------- */

static int32_t svc_bt_l2_send(const struct canopus_context_v1 *ctx, uint8_t channel,
                              const void *payload, uint16_t len)
{
    (void)ctx;
    (void)channel;
    (void)payload;
    (void)len;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}

static uint32_t svc_bt_adapter_state(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0u;
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

    canopus_hook_engine_init();

    /* Wire legacy services v1 table */
    canopus_memset(&s_services, 0, sizeof(s_services));
    canopus_memset(&s_fs_obj, 0, sizeof(s_fs_obj));
    canopus_memset(&s_mem_obj, 0, sizeof(s_mem_obj));
    canopus_memset(&s_time_obj, 0, sizeof(s_time_obj));
    canopus_memset(&s_hook_obj, 0, sizeof(s_hook_obj));
    canopus_memset(&s_log_obj, 0, sizeof(s_log_obj));
    canopus_memset(&s_notify_obj, 0, sizeof(s_notify_obj));
    canopus_memset(&s_system_obj, 0, sizeof(s_system_obj));
    canopus_memset(&s_bt_obj, 0, sizeof(s_bt_obj));

    s_services.struct_size = sizeof(s_services);
    s_services.group_version[CANOPUS_SVC_MODULE] = 1u;
    s_services.group_version[CANOPUS_SVC_LOG] = 1u;
    s_services.group_version[CANOPUS_SVC_HOOK] = 1u;
    s_services.group_version[CANOPUS_SVC_SYSTEM] = 1u;
    s_services.group_version[CANOPUS_SVC_BLUETOOTH] = 1u;

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

        s_fs_obj.open = svc_fs_open_obj;
        s_fs_obj.read = svc_fs_read;
        s_fs_obj.write = svc_fs_write;
        s_fs_obj.seek = svc_fs_seek;
        s_fs_obj.tell = svc_fs_tell;
        s_fs_obj.size = svc_fs_size;
        s_fs_obj.sync = svc_fs_sync;
        s_fs_obj.close = svc_fs_close;
        s_fs_obj.remove = svc_fs_remove;
        s_fs_obj.rename = svc_fs_rename;
        s_fs_obj.stat = svc_fs_stat;
        s_fs_obj.exists = svc_fs_exists;
        s_fs_obj.mkdir = svc_fs_mkdir;
        s_fs_obj.opendir = svc_fs_opendir;
        s_fs_obj.readdir = svc_fs_readdir;
        s_fs_obj.closedir = svc_fs_closedir;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->monotonic_ms != 0) {
        s_services.group_version[CANOPUS_SVC_TIME] = 1u;
        s_services.time_monotonic_ms = svc_time_monotonic_ms;
        s_time_obj.monotonic_ms = svc_time_monotonic_ms;
        s_time_obj.realtime_ms = svc_time_realtime_ms;
        s_time_obj.sleep_ms = svc_time_sleep_ms;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->heap_alloc != 0 &&
        sup->platform->heap_release != 0) {
        s_services.group_version[CANOPUS_SVC_MEM] = 1u;
        s_services.mem_alloc = svc_mem_alloc;
        s_services.mem_free = svc_mem_free;
        s_mem_obj.alloc = svc_mem_alloc;
        s_mem_obj.realloc = svc_mem_realloc;
        s_mem_obj.free = svc_mem_free;
        s_mem_obj.available = svc_mem_available;
        s_mem_obj.used = svc_mem_used;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->notify != 0) {
        s_services.group_version[CANOPUS_SVC_NOTIFY] = 1u;
        s_services.notify_post = svc_notify_post;
        s_notify_obj.post = svc_notify_post_obj;
    }

    s_hook_obj.install = svc_hook_install;
    s_hook_obj.uninstall = svc_hook_uninstall;

    s_log_obj.write = svc_log_write;
    s_log_obj.info = svc_log_info;
    s_log_obj.warn = svc_log_warn;
    s_log_obj.error = svc_log_error;
    s_log_obj.debug = svc_log_debug;

    s_system_obj.target_id = svc_system_target_id;
    s_system_obj.framework_revision = svc_system_framework_revision;
    s_system_obj.is_safe_mode = svc_system_is_safe_mode;
    s_system_obj.reboot = svc_system_reboot;

    s_bt_obj.l2_send = svc_bt_l2_send;
    s_bt_obj.adapter_state = svc_bt_adapter_state;

    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        s_contexts[i].struct_size = sizeof(struct canopus_context_v1);
        s_contexts[i].abi = (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR;
        s_contexts[i].services = &s_services;
        s_contexts[i].slot = i;
        s_contexts[i].target_id = (sup != 0 && sup->platform != 0) ? sup->platform->target_id : "";
        s_contexts[i].module_id = (const char *)sup->modules[i].module_id;

        s_contexts[i].fs = (s_services.group_version[CANOPUS_SVC_FS] != 0u) ? &s_fs_obj : 0;
        s_contexts[i].mem = (s_services.group_version[CANOPUS_SVC_MEM] != 0u) ? &s_mem_obj : 0;
        s_contexts[i].time = (s_services.group_version[CANOPUS_SVC_TIME] != 0u) ? &s_time_obj : 0;
        s_contexts[i].hook = &s_hook_obj;
        s_contexts[i].log = &s_log_obj;
        s_contexts[i].notify = (s_services.group_version[CANOPUS_SVC_NOTIFY] != 0u) ? &s_notify_obj : 0;
        s_contexts[i].system = &s_system_obj;
        s_contexts[i].bt = &s_bt_obj;
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
    if (sup == 0 || sup != s_sup || slot >= CANOPUS_SUP_MODULE_SLOTS) {
        return;
    }

    /* Release all active hooks owned by this module */
    canopus_hook_release_slot(slot);

    if (sup->platform != 0 && sup->platform->file_close != 0) {
        for (i = 0u; i < CANOPUS_FS_MAX_OPEN; i++) {
            if (s_fd[slot][i] != 0) {
                (void)sup->platform->file_close(sup->platform_cookie,
                                                s_fd[slot][i] - 1);
                s_fd[slot][i] = 0;
            }
        }
    }
    s_notify_posts[slot] = 0u;
    if (sup->platform != 0 && sup->platform->heap_release != 0) {
        struct svc_mem_block *block = s_mem_head[slot];
        while (block != 0) {
            struct svc_mem_block *next = block->next;
            sup->platform->heap_release(sup->platform_cookie, block);
            block = next;
        }
        s_mem_head[slot] = 0;
    }
}
