/* canopus_supervisor_services.c — Supervisor service table & Object system (ADR-CAN-006).
 *
 * Generic capabilities are implemented once per firmware inside the
 * Supervisor and handed to modules through `canopus_context_v1`.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_supervisor_boot.h"
#include "canopus_memory.h"
#include "../../runtime/hook/canopus_hook.h"
#include "canopus_ui.h"

#if defined(CANOPUS_HOST) && (!defined(__STDC_HOSTED__) || __STDC_HOSTED__ != 0)
#include <time.h>
#include <errno.h>
#define CANOPUS_HAS_NANOSLEEP 1
#endif


#define SVC_LOG_ENTRIES 64u
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
static struct canopus_kv_obj s_kv_obj;
static struct canopus_mem_obj s_mem_obj;
static struct canopus_time_obj s_time_obj;
static struct canopus_hook_obj s_hook_obj;
static struct canopus_log_obj s_log_obj;
static struct canopus_notify_obj s_notify_obj;
static struct canopus_system_obj s_system_obj;
static struct canopus_bluetooth_obj s_bt_obj;
static const struct canopus_connect_obj *s_connect_obj;
static struct {
    struct canopus_ui_entry_v1 entry;
    char label[CANOPUS_UI_ENTRY_LABEL_MAX];
    uint8_t used;
} s_ui_entries[CANOPUS_SUP_MODULE_SLOTS];
static struct canopus_launcher_obj s_launcher_obj;
/* The launcher entry a module published. The firmware cannot take it back, so
 * it stays until reboot and is not cleared when the module is released. */
static struct {
    char label[CANOPUS_LAUNCHER_LABEL_MAX];
    char icon[CANOPUS_LAUNCHER_ICON_MAX];
    uint8_t used;
} s_launcher[CANOPUS_SUP_MODULE_SLOTS];
static uint8_t s_launcher_pending; /* entries are queued in the platform */
static struct canopus_haptics_obj s_haptics_obj;
static struct canopus_display_obj s_display_obj;
static struct canopus_bus_obj s_bus_obj;
static struct canopus_ui_obj s_ui_obj;

static struct svc_log_entry s_log[SVC_LOG_ENTRIES];
static uint32_t s_log_total;
static int s_fd[CANOPUS_SUP_MODULE_SLOTS][CANOPUS_FS_MAX_OPEN];

struct svc_mem_block {
    struct svc_mem_block *next;
    uint32_t size;
};

static struct svc_mem_block *s_mem_head[CANOPUS_SUP_MODULE_SLOTS];
static uint32_t s_notify_posts[CANOPUS_SUP_MODULE_SLOTS];

/* KV values stay in each module's file: every operation scans it one entry at
 * a time through s_kv_entry, so the table costs no resident memory. */
struct kv_disk_entry {
    char key[CANOPUS_KV_KEY_MAX];
    char val[CANOPUS_KV_VAL_MAX];
};
static struct kv_disk_entry s_kv_entry;

/* Timer State */
#define CANOPUS_MAX_TIMERS 64u
struct module_timer {
    uint32_t slot;
    uint32_t interval_ms;
    uint32_t next_fire_ms;
    int periodic;
    int running;
    int used;
    int background; /* also dispatched while the screen is off */
    canopus_timer_callback_t cb;
    void *user_data;
};
static struct module_timer s_timers[CANOPUS_MAX_TIMERS];
static int s_timer_dispatching;
static int s_timer_pump_started;
static int s_background_pump_started;

/* Bus State */
#define CANOPUS_BUS_MAX_SUBS 64u
struct bus_subscription {
    uint32_t slot;
    char topic[32];
    canopus_bus_callback_t cb;
    void *user_data;
    int used;
};
static struct bus_subscription s_bus_subs[CANOPUS_BUS_MAX_SUBS];
static int s_bus_dispatching;
static uint32_t s_module_events;

/* One coalesced owner-task UI job per module; release cancels it. The ready
 * mask is frozen before draining, so reposts wait for the next heartbeat. */
static struct {
    canopus_ui_task_fn callback;
    void *user_data;
} s_ui_jobs[CANOPUS_SUP_MODULE_SLOTS];
static uint32_t s_ui_jobs_ready;

/* Display State */
struct svc_wakelock {
    uint16_t generation;
    uint8_t used;
    uint8_t slot;
};
static struct svc_wakelock s_wakelocks[CANOPUS_WAKELOCK_MAX];
static uint16_t s_wakelock_generation;
static int32_t s_brightness_slot = -1;
static uint32_t s_brightness_percent;

/* Haptics State: one vibration plays at a time. */
static struct {
    uint8_t active;
    uint8_t slot;
    uint8_t count;
    uint8_t step;
    uint8_t started;
    uint32_t next; /* scheduled start of `step` */
    uint16_t pattern[CANOPUS_HAPTIC_PATTERN_MAX];
} s_haptic;

/* System event sampling (screen, battery, Bluetooth) for the bus topics. */
#define SVC_EVENT_SCREEN 1u
#define SVC_EVENT_BATTERY 2u
#define SVC_EVENT_BT 4u
#define SVC_EVENT_PERIOD_MS 500u
static struct {
    uint32_t wanted;
    uint32_t known;
    uint32_t next_ms;
    uint8_t screen;
    uint8_t battery[2];
    uint8_t bt[2];
} s_events;

static int32_t svc_pump_ensure(void);

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

/* A module reaches any absolute path on the device except Canopus's own
 * state; a relative name lives in the module's private directory
 * /data/canopus/m.<id>/. Absolute paths are canonicalized (".", ".." and "//"
 * resolved) BEFORE the protection check, so "/data/x/../canopus/registry.bin"
 * is refused like "/data/canopus/registry.bin". See canopus_services.h. */
struct svc_fs_path {
    char path[SVC_FS_PATH_MAX];
    /* Directory that must exist before a write below `path` ("" = nothing to
     * create). Created best-effort, one level, when the write is attempted. */
    char ensure[SVC_FS_PATH_MAX];
};

static uint32_t svc_append(char *out, uint32_t used, const char *text)
{
    uint32_t i;
    for (i = 0u; text[i] != '\0'; i++) {
        if (used + 1u >= SVC_FS_PATH_MAX) return SVC_FS_PATH_MAX;
        out[used++] = text[i];
    }
    return used;
}

static uint32_t svc_str_len(const char *s)
{
    uint32_t n = 0u;
    while (s[n] != '\0') n++;
    return n;
}

static uint32_t svc_private_dir(const struct canopus_context_v1 *ctx,
                                char out[SVC_FS_PATH_MAX])
{
    const char *id = (const char *)s_sup->modules[ctx->slot].module_id;
    uint32_t used = svc_append(out, 0u, SVC_FS_ROOT);
    if (used < SVC_FS_PATH_MAX) used = svc_append(out, used, id);
    if (used < SVC_FS_PATH_MAX) out[used] = '\0';
    return used;
}

/* Lexically resolves `src` into `out`: empty and "." segments vanish, ".."
 * removes the previous segment and is refused above the start, control bytes
 * are refused. An absolute result starts with '/' ("/" alone is the root); a
 * relative result has no leading '/' and must not be empty. */
static int svc_canonical(const char *src, int absolute, char out[SVC_FS_PATH_MAX])
{
    uint32_t used = 0u, i = absolute ? 1u : 0u;
    while (src[i] != '\0') {
        uint32_t start = i, len;
        while (src[i] != '\0' && src[i] != '/') {
            unsigned char c = (unsigned char)src[i];
            if (c < 0x20u || c == 0x7Fu) return 0;
            i++;
        }
        len = i - start;
        if (src[i] == '/') i++;
        if (len == 0u || (len == 1u && src[start] == '.')) continue;
        if (len == 2u && src[start] == '.' && src[start + 1u] == '.') {
            if (used == 0u) return 0;
            while (used > 0u && out[used - 1u] != '/') used--;
            if (used > 0u) used--; /* the '/' before the removed segment */
            continue;
        }
        if (used + len + 1u >= SVC_FS_PATH_MAX) return 0;
        if (absolute || used > 0u) out[used++] = '/';
        for (; len > 0u; len--) out[used++] = src[start++];
    }
    if (absolute && used == 0u) out[used++] = '/';
    out[used] = '\0';
    return used > 0u;
}

static char svc_lower(char c)
{
    return c >= 'A' && c <= 'Z' ? (char)(c - 'A' + 'a') : c;
}

/* `prefix` is lowercase; the match ends at a path-segment boundary. The case
 * fold covers a case-insensitive filesystem aliasing the same directory. */
static int svc_path_under(const char *path, const char *prefix)
{
    uint32_t i;
    for (i = 0u; prefix[i] != '\0'; i++) {
        if (svc_lower(path[i]) != prefix[i]) return 0;
    }
    return path[i] == '\0' || path[i] == '/';
}

/* Canopus's own state: the module registry and inbox, KV files and every
 * module's private directory, the control nodes. A module keeps its own
 * private directory (exact case) and nothing else of it. */
static int svc_fs_protected(const struct canopus_context_v1 *ctx, const char *path)
{
    static const char *const owned[] = {"/data/canopus", "/dev/canopus", "/canopus"};
    char own[SVC_FS_PATH_MAX];
    uint32_t i, n;
    n = svc_private_dir(ctx, own);
    for (i = 0u; i < n && path[i] == own[i]; i++) {}
    if (i == n && (path[i] == '\0' || path[i] == '/')) return 0;
    for (i = 0u; i < sizeof(owned) / sizeof(owned[0]); i++) {
        if (svc_path_under(path, owned[i])) return 1;
    }
    return 0;
}

/* Resolves a module-supplied name to a platform path. Returns OK, ARGUMENT
 * for a malformed path, ACCESS for Canopus-owned state. */
static int32_t svc_fs_resolve(const struct canopus_context_v1 *ctx, const char *name,
                              struct svc_fs_path *out)
{
    char canonical[SVC_FS_PATH_MAX];
    uint32_t used;
    out->ensure[0] = '\0';
    if (name == 0 || name[0] == '\0' || svc_str_len(name) >= SVC_FS_PATH_MAX) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (name[0] == '/') {
        if (!svc_canonical(name, 1, canonical)) return CANOPUS_SVC_ERR_ARGUMENT;
        if (svc_fs_protected(ctx, canonical)) return CANOPUS_SVC_ERR_ACCESS;
        used = svc_append(out->path, 0u, canonical);
        if (used >= SVC_FS_PATH_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
        out->path[used] = '\0';
        return CANOPUS_SVC_OK;
    }
    if (!svc_canonical(name, 0, canonical)) return CANOPUS_SVC_ERR_ARGUMENT;
    used = svc_private_dir(ctx, out->path);
    if (used >= SVC_FS_PATH_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
    (void)svc_append(out->ensure, 0u, out->path);
    out->ensure[used] = '\0';
    used = svc_append(out->path, used, "/");
    used = svc_append(out->path, used, canonical);
    if (used >= SVC_FS_PATH_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
    out->path[used] = '\0';
    return CANOPUS_SVC_OK;
}

/* Platform results are 0/positive or a negated errno (a bare -1 is an
 * unattributed failure). Codes the service ABI names pass through. */
static int32_t svc_errno(int32_t rc)
{
    switch (rc) {
    case -2:  return CANOPUS_SVC_ERR_NOTFOUND;
    case -5:  return CANOPUS_SVC_ERR_IO;
    case -12: return CANOPUS_SVC_ERR_NOMEM;
    case -13: return CANOPUS_SVC_ERR_ACCESS;
    case -16: return CANOPUS_SVC_ERR_BUSY;
    case -17: return CANOPUS_SVC_ERR_EXIST;
    case -20: return CANOPUS_SVC_ERR_NOTDIR;
    case -21: return CANOPUS_SVC_ERR_ISDIR;
    case -22: return CANOPUS_SVC_ERR_ARGUMENT;
    case -28: return CANOPUS_SVC_ERR_NOSPACE;
    case -38: return CANOPUS_SVC_ERR_UNSUPPORTED;
    default:  return rc >= 0 ? rc : CANOPUS_SVC_ERR_IO;
    }
}

static void svc_fs_ensure(const struct svc_fs_path *p)
{
    if (p->ensure[0] != '\0' && s_sup->platform->dir_mkdir != 0) {
        (void)s_sup->platform->dir_mkdir(s_sup->platform_cookie, p->ensure);
    }
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

#define SVC_FS_FLAG_MASK (CANOPUS_FS_READ | CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | \
                          CANOPUS_FS_CREATE | CANOPUS_FS_TRUNC)

static int32_t svc_fs_open_flags(const struct canopus_context_v1 *ctx,
                                 const char *name, uint32_t flags)
{
    struct svc_fs_path p;
    uint32_t i;
    int32_t rc;
    int fd;
    int write = (flags & CANOPUS_FS_WRITE) != 0u;
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (flags == 0u || (flags & ~SVC_FS_FLAG_MASK) != 0u ||
        (!write && (flags & (CANOPUS_FS_APPEND | CANOPUS_FS_CREATE | CANOPUS_FS_TRUNC)) != 0u)) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    rc = svc_fs_resolve(ctx, name, &p);
    if (rc != CANOPUS_SVC_OK) return rc;
    for (i = 0u; i < CANOPUS_FS_MAX_OPEN; i++) {
        if (s_fd[ctx->slot][i] == 0) break;
    }
    if (i == CANOPUS_FS_MAX_OPEN) return CANOPUS_SVC_ERR_STATE;
    if (write) svc_fs_ensure(&p);
    fd = s_sup->platform->file_open(s_sup->platform_cookie, p.path, flags);
    if (fd < 0) return svc_errno(fd);
    s_fd[ctx->slot][i] = fd + 1;
    return (int32_t)i;
}

/* Table v1 entry: exactly READ or WRITE (create/truncate). */
static int32_t svc_fs_open(const struct canopus_context_v1 *ctx,
                           const char *name, uint32_t flags)
{
    if (flags != CANOPUS_FS_READ && flags != CANOPUS_FS_WRITE) {
        return svc_fs_ready(ctx) ? CANOPUS_SVC_ERR_ARGUMENT : CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    return svc_fs_open_flags(ctx, name, flags);
}

static int32_t svc_fs_read(const struct canopus_context_v1 *ctx, int32_t handle,
                           void *buffer, uint32_t size)
{
    int32_t rc;
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (buffer == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    rc = s_sup->platform->file_read(s_sup->platform_cookie,
                                    s_fd[ctx->slot][handle] - 1, buffer, size);
    return rc < 0 ? svc_errno(rc) : rc;
}

static int32_t svc_fs_write(const struct canopus_context_v1 *ctx, int32_t handle,
                            const void *buffer, uint32_t size)
{
    int32_t rc;
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (buffer == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    rc = s_sup->platform->file_write(s_sup->platform_cookie,
                                     s_fd[ctx->slot][handle] - 1, buffer, size);
    return rc < 0 ? svc_errno(rc) : rc;
}

static int64_t svc_fs_seek(const struct canopus_context_v1 *ctx, int32_t handle,
                           int64_t offset, int32_t whence)
{
    int64_t rc;
    if (!svc_handle_ok(ctx, handle) || s_sup->platform->file_seek == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = s_sup->platform->file_seek(s_sup->platform_cookie,
                                    s_fd[ctx->slot][handle] - 1, offset, whence);
    return rc < 0 ? svc_errno((int32_t)rc) : rc;
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
    if (cur < 0 || end < 0) return CANOPUS_SVC_ERR_IO;
    (void)s_sup->platform->file_seek(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1, cur, CANOPUS_FS_SEEK_SET);
    return end;
}

static int32_t svc_fs_sync(const struct canopus_context_v1 *ctx, int32_t handle)
{
    int rc;
    if (!svc_handle_ok(ctx, handle)) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform->file_sync == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = s_sup->platform->file_sync(s_sup->platform_cookie, s_fd[ctx->slot][handle] - 1);
    return rc < 0 ? svc_errno(rc) : CANOPUS_SVC_OK;
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
    struct svc_fs_path p;
    int32_t rc;
    if (!svc_fs_ready(ctx)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = svc_fs_resolve(ctx, name, &p);
    if (rc != CANOPUS_SVC_OK) return rc;
    rc = s_sup->platform->file_remove(s_sup->platform_cookie, p.path);
    return rc < 0 ? svc_errno(rc) : CANOPUS_SVC_OK;
}

static int32_t svc_fs_rename(const struct canopus_context_v1 *ctx,
                             const char *old_path, const char *new_path)
{
    struct svc_fs_path from, to;
    int32_t rc;
    if (!svc_fs_ready(ctx) || s_sup->platform->file_rename == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = svc_fs_resolve(ctx, old_path, &from);
    if (rc != CANOPUS_SVC_OK) return rc;
    rc = svc_fs_resolve(ctx, new_path, &to);
    if (rc != CANOPUS_SVC_OK) return rc;
    svc_fs_ensure(&to);
    rc = s_sup->platform->file_rename(s_sup->platform_cookie, from.path, to.path);
    return rc < 0 ? svc_errno(rc) : CANOPUS_SVC_OK;
}

static int32_t svc_fs_stat(const struct canopus_context_v1 *ctx, const char *path,
                           struct canopus_fs_stat *out_stat)
{
    struct svc_fs_path p;
    int32_t rc;
    if (!svc_fs_ready(ctx) || out_stat == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    rc = svc_fs_resolve(ctx, path, &p);
    if (rc != CANOPUS_SVC_OK) return rc;
    if (s_sup->platform->file_stat != 0) {
        rc = s_sup->platform->file_stat(s_sup->platform_cookie, p.path, out_stat);
        return rc < 0 ? svc_errno(rc) : CANOPUS_SVC_OK;
    }
    {
        int fd = s_sup->platform->file_open(s_sup->platform_cookie, p.path, CANOPUS_FS_READ);
        if (fd < 0) return svc_errno(fd);
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
    struct svc_fs_path p;
    int32_t rc;
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_mkdir == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = svc_fs_resolve(ctx, path, &p);
    if (rc != CANOPUS_SVC_OK) return rc;
    svc_fs_ensure(&p);
    rc = s_sup->platform->dir_mkdir(s_sup->platform_cookie, p.path);
    return rc < 0 ? svc_errno(rc) : CANOPUS_SVC_OK;
}

/* "" and "." name the module's private directory. */
static void *svc_fs_opendir(const struct canopus_context_v1 *ctx, const char *path)
{
    struct svc_fs_path p;
    if (!svc_fs_ready(ctx) || s_sup->platform->dir_open == 0 || path == 0) return 0;
    if (path[0] == '\0' || (path[0] == '.' && path[1] == '\0')) {
        (void)svc_private_dir(ctx, p.path);
    } else if (svc_fs_resolve(ctx, path, &p) != CANOPUS_SVC_OK) {
        return 0;
    }
    return s_sup->platform->dir_open(s_sup->platform_cookie, p.path);
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

static int32_t svc_fs_read_all(const struct canopus_context_v1 *ctx, const char *path,
                               void *buffer, uint32_t max_size)
{
    int32_t fd = svc_fs_open(ctx, path, CANOPUS_FS_READ);
    int32_t rd;
    if (fd < 0) return fd;
    rd = svc_fs_read(ctx, fd, buffer, max_size);
    svc_fs_close(ctx, fd);
    return rd;
}

static int32_t svc_fs_write_all(const struct canopus_context_v1 *ctx, const char *path,
                                const void *buffer, uint32_t size)
{
    int32_t fd = svc_fs_open(ctx, path, CANOPUS_FS_WRITE);
    int32_t wr;
    if (fd < 0) return fd;
    wr = svc_fs_write(ctx, fd, buffer, size);
    svc_fs_close(ctx, fd);
    return wr;
}

static int32_t svc_fs_get_data_path(const struct canopus_context_v1 *ctx,
                                    char *buffer, uint32_t max_size)
{
    char own[SVC_FS_PATH_MAX];
    uint32_t len;
    if (!svc_slot_valid(ctx) || buffer == 0 || max_size == 0u) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    len = svc_private_dir(ctx, own);
    if (len + 1u > max_size) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    canopus_memcpy(buffer, own, len + 1u);
    return (int32_t)len;
}

/* ---- KV (Persistent Key-Value Store) --------------------------------- */

static int32_t parse_int32(const char *s, int32_t default_val)
{
    int neg = 0;
    uint32_t uval = 0u;
    if (s == 0 || *s == '\0') return default_val;
    if (*s == '-') { neg = 1; s++; }
    else if (*s == '+') { s++; }
    if (*s < '0' || *s > '9') return default_val;
    while (*s >= '0' && *s <= '9') {
        uval = uval * 10u + (uint32_t)(*s - '0');
        s++;
    }
    if (neg) {
        return (int32_t)(0u - uval);
    }
    return (int32_t)uval;
}

static void format_int32(char buf[24], int32_t val)
{
    char tmp[24];
    int i = 0, j = 0;
    uint32_t uval;
    if (val < 0) {
        buf[j++] = '-';
        uval = 0u - (uint32_t)val;
    } else {
        uval = (uint32_t)val;
    }
    if (uval == 0) {
        buf[j++] = '0';
        buf[j] = '\0';
        return;
    }
    while (uval > 0) {
        tmp[i++] = (char)('0' + (uval % 10));
        uval /= 10;
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

#define CANOPUS_KV_FILE_MAGIC 0x43504B56u /* 'CPKV' */

struct kv_disk_header {
    uint32_t magic;
    uint32_t count;
};

struct kv_scan {
    int fd; /* -1 while the module has no KV file */
    uint32_t count;
};

static int svc_kv_path(uint32_t slot, char out[SVC_FS_PATH_MAX])
{
    const char *id;
    const char *prefix = "/data/canopus/kv_";
    uint32_t used = 0u, i;
    if (s_sup == 0 || slot >= CANOPUS_SUP_MODULE_SLOTS) return -1;
    id = (const char *)s_sup->modules[slot].module_id;
    if (id == 0 || id[0] == '\0') return -1;
    for (i = 0u; prefix[i] != '\0'; i++) out[used++] = prefix[i];
    for (i = 0u; i < CANOPUS_SUP_MODULE_ID_MAX && id[i] != '\0'; i++) {
        if (used + 5u >= SVC_FS_PATH_MAX) return -1;
        out[used++] = id[i];
    }
    out[used++] = '.';
    out[used++] = 'b';
    out[used++] = 'i';
    out[used++] = 'n';
    out[used] = '\0';
    return 0;
}

static int32_t svc_kv_transfer(int fd, void *buffer, uint32_t size, int write)
{
    uint32_t used = 0u;
    uint8_t *bytes = (uint8_t *)buffer;
    const struct canopus_sup_platform_v1 *pf = s_sup->platform;
    while (used < size) {
        int32_t n = write
            ? pf->file_write(s_sup->platform_cookie, fd, bytes + used, size - used)
            : pf->file_read(s_sup->platform_cookie, fd, bytes + used, size - used);
        if (n <= 0 || (uint32_t)n > size - used) return CANOPUS_SVC_ERR_IO;
        used += (uint32_t)n;
    }
    return CANOPUS_SVC_OK;
}

static int32_t svc_kv_close(struct kv_scan *scan)
{
    int fd = scan->fd;
    scan->fd = -1;
    if (fd < 0) return CANOPUS_SVC_OK;
    return s_sup->platform->file_close(s_sup->platform_cookie, fd) != 0
        ? CANOPUS_SVC_ERR_IO : CANOPUS_SVC_OK;
}

static int32_t svc_kv_open(uint32_t slot, struct kv_scan *scan)
{
    char path[SVC_FS_PATH_MAX];
    struct kv_disk_header hdr;
    const struct canopus_sup_platform_v1 *pf = s_sup->platform;
    int32_t rc;
    if (pf == 0 || pf->file_open == 0 || pf->file_read == 0 || pf->file_close == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    if (svc_kv_path(slot, path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    scan->count = 0u;
    scan->fd = pf->file_open(s_sup->platform_cookie, path, CANOPUS_FS_READ);
    if (scan->fd == CANOPUS_SVC_ERR_NOTFOUND) {
        scan->fd = -1;
        return CANOPUS_SVC_OK;
    }
    if (scan->fd < 0) return CANOPUS_SVC_ERR_IO;
    rc = svc_kv_transfer(scan->fd, &hdr, (uint32_t)sizeof(hdr), 0);
    if (rc == 0 && (hdr.magic != CANOPUS_KV_FILE_MAGIC || hdr.count > CANOPUS_KV_MAX_PAIRS)) {
        rc = CANOPUS_SVC_ERR_IO;
    }
    if (rc != 0) {
        (void)svc_kv_close(scan);
        return rc;
    }
    scan->count = hdr.count;
    return CANOPUS_SVC_OK;
}

/* Reads entries into s_kv_entry up to and including the one named `key`.
 * *index is its position, or scan->count when the key is absent. */
static int32_t svc_kv_find(struct kv_scan *scan, const char *key, uint32_t *index)
{
    uint32_t i;
    for (i = 0u; i < scan->count; i++) {
        int32_t rc = svc_kv_transfer(scan->fd, &s_kv_entry,
                                     (uint32_t)sizeof(s_kv_entry), 0);
        if (rc != 0) return rc;
        if (canopus_strcmp(s_kv_entry.key, key) == 0) break;
    }
    *index = i;
    return CANOPUS_SVC_OK;
}

static int32_t svc_kv_check_key(const struct canopus_context_v1 *ctx, const char *key)
{
    if (!svc_slot_valid(ctx) || key == 0 || key[0] == '\0' ||
        canopus_strlen(key) >= CANOPUS_KV_KEY_MAX) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    return CANOPUS_SVC_OK;
}

static void svc_kv_fill(const char *key, const char *value)
{
    canopus_memset(&s_kv_entry, 0, sizeof(s_kv_entry));
    canopus_strncpy(s_kv_entry.key, key, sizeof(s_kv_entry.key) - 1u);
    canopus_strncpy(s_kv_entry.val, value, sizeof(s_kv_entry.val) - 1u);
}

/* Sets `key` to `value`, removes it when `value` is null, or removes every pair
 * when `key` is null. The new file is written beside the old one and renamed
 * over it, so a failure at any step leaves the previous file intact. */
static int32_t svc_kv_update(uint32_t slot, const char *key, const char *value)
{
    char path[SVC_FS_PATH_MAX];
    char tmp[SVC_FS_PATH_MAX];
    struct kv_scan scan = { -1, 0u };
    struct kv_disk_header hdr = { CANOPUS_KV_FILE_MAGIC, 0u };
    const struct canopus_sup_platform_v1 *pf = s_sup->platform;
    uint32_t i, len, index = 0u;
    int fd, found = 0;
    int32_t rc;
    if (pf == 0 || pf->file_open == 0 || pf->file_write == 0 ||
        pf->file_close == 0 || pf->file_rename == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    if (svc_kv_path(slot, path) != 0) return CANOPUS_SVC_ERR_ARGUMENT;
    len = (uint32_t)canopus_strlen(path);
    if (len + sizeof(".tmp") > sizeof(tmp)) return CANOPUS_SVC_ERR_ARGUMENT;
    canopus_memcpy(tmp, path, len);
    canopus_memcpy(tmp + len, ".tmp", sizeof(".tmp"));

    if (key != 0) {
        rc = svc_kv_open(slot, &scan);
        if (rc == 0) rc = svc_kv_find(&scan, key, &index);
        if (svc_kv_close(&scan) != 0 && rc == 0) rc = CANOPUS_SVC_ERR_IO;
        if (rc != 0) return rc;
        found = index < scan.count;
        if (!found && value == 0) return CANOPUS_SVC_ERR_NOTFOUND;
        if (!found && scan.count == CANOPUS_KV_MAX_PAIRS) return CANOPUS_SVC_ERR_NOMEM;
        hdr.count = scan.count + (value != 0 && !found ? 1u : 0u) - (value == 0 ? 1u : 0u);
        rc = svc_kv_open(slot, &scan);
        if (rc != 0) return rc;
    }
    fd = pf->file_open(s_sup->platform_cookie, tmp, CANOPUS_FS_WRITE);
    if (fd < 0) {
        (void)svc_kv_close(&scan);
        return CANOPUS_SVC_ERR_IO;
    }
    rc = svc_kv_transfer(fd, &hdr, (uint32_t)sizeof(hdr), 1);
    for (i = 0u; rc == 0 && i < scan.count; i++) {
        rc = svc_kv_transfer(scan.fd, &s_kv_entry, (uint32_t)sizeof(s_kv_entry), 0);
        if (rc == 0 && i == index) {
            if (value == 0) continue;
            svc_kv_fill(key, value);
        }
        if (rc == 0) rc = svc_kv_transfer(fd, &s_kv_entry, (uint32_t)sizeof(s_kv_entry), 1);
    }
    if (rc == 0 && value != 0 && !found) {
        svc_kv_fill(key, value);
        rc = svc_kv_transfer(fd, &s_kv_entry, (uint32_t)sizeof(s_kv_entry), 1);
    }
    if (svc_kv_close(&scan) != 0 && rc == 0) rc = CANOPUS_SVC_ERR_IO;
    if (rc == 0 && pf->file_sync != 0 &&
        pf->file_sync(s_sup->platform_cookie, fd) != 0) rc = CANOPUS_SVC_ERR_IO;
    if (pf->file_close(s_sup->platform_cookie, fd) != 0) rc = CANOPUS_SVC_ERR_IO;
    if (rc == 0 && pf->file_rename(s_sup->platform_cookie, tmp, path) != 0) {
        rc = CANOPUS_SVC_ERR_IO;
    }
    if (rc != 0 && pf->file_remove != 0) {
        (void)pf->file_remove(s_sup->platform_cookie, tmp);
    }
    return rc;
}

static int32_t svc_kv_get_string(const struct canopus_context_v1 *ctx, const char *key,
                                 char *buf, uint32_t buf_size)
{
    struct kv_scan scan;
    uint32_t index, len = 0u;
    int32_t rc;
    if (buf == 0 || buf_size == 0u) return CANOPUS_SVC_ERR_ARGUMENT;
    rc = svc_kv_check_key(ctx, key);
    if (rc != 0) return rc;
    rc = svc_kv_open(ctx->slot, &scan);
    if (rc == 0) rc = svc_kv_find(&scan, key, &index);
    if (svc_kv_close(&scan) != 0 && rc == 0) rc = CANOPUS_SVC_ERR_IO;
    if (rc != 0) return rc;
    if (index == scan.count) return CANOPUS_SVC_ERR_NOTFOUND;
    while (s_kv_entry.val[len] != '\0' && len + 1u < buf_size) {
        buf[len] = s_kv_entry.val[len];
        len++;
    }
    buf[len] = '\0';
    return (int32_t)len;
}

static int32_t svc_kv_set_string(const struct canopus_context_v1 *ctx, const char *key,
                                 const char *val)
{
    int32_t rc = svc_kv_check_key(ctx, key);
    if (rc != 0) return rc;
    if (val == 0 || canopus_strlen(val) >= CANOPUS_KV_VAL_MAX) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    return svc_kv_update(ctx->slot, key, val);
}

static int32_t svc_kv_get_int(const struct canopus_context_v1 *ctx, const char *key,
                              int32_t default_val)
{
    char buf[24];
    if (svc_kv_get_string(ctx, key, buf, sizeof(buf)) <= 0) {
        return default_val;
    }
    return parse_int32(buf, default_val);
}

static int32_t svc_kv_set_int(const struct canopus_context_v1 *ctx, const char *key,
                              int32_t val)
{
    char buf[24];
    format_int32(buf, val);
    return svc_kv_set_string(ctx, key, buf);
}

static int svc_kv_get_bool(const struct canopus_context_v1 *ctx, const char *key,
                           int default_val)
{
    char buf[12];
    if (svc_kv_get_string(ctx, key, buf, sizeof(buf)) <= 0) {
        return default_val;
    }
    return (buf[0] == '1' || buf[0] == 't' || buf[0] == 'T') ? 1 : 0;
}

static int32_t svc_kv_set_bool(const struct canopus_context_v1 *ctx, const char *key,
                               int val)
{
    return svc_kv_set_string(ctx, key, val ? "1" : "0");
}

static int32_t svc_kv_remove(const struct canopus_context_v1 *ctx, const char *key)
{
    int32_t rc = svc_kv_check_key(ctx, key);
    if (rc != 0) return rc;
    return svc_kv_update(ctx->slot, key, 0);
}

static int32_t svc_kv_clear(const struct canopus_context_v1 *ctx)
{
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_ARGUMENT;
    return svc_kv_update(ctx->slot, 0, 0);
}

/* ---- MEM (Heap Allocator) -------------------------------------------- */

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

static void *svc_mem_calloc(const struct canopus_context_v1 *ctx, uint32_t num, uint32_t size)
{
    uint32_t total;
    void *p;
    if (num != 0 && size > UINT32_MAX / num) return 0;
    total = num * size;
    p = svc_mem_alloc(ctx, total);
    if (p != 0) canopus_memset(p, 0, total);
    return p;
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
    if (new_size > UINT32_MAX - (uint32_t)sizeof(struct svc_mem_block)) return 0;
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

/* ---- TIME & TIMERS --------------------------------------------------- */

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

static void svc_haptic_poll(uint32_t now);
static void svc_events_poll(uint32_t now);
static void svc_launcher_poll(void);

int32_t canopus_supervisor_ui_pump_ensure(struct canopus_supervisor_v1 *sup)
{
    return sup == s_sup ? svc_pump_ensure() : CANOPUS_SVC_ERR_STATE;
}

int32_t canopus_supervisor_ui_post(const struct canopus_context_v1 *ctx,
                                  canopus_ui_task_fn callback, void *user_data)
{
    int32_t rc;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (callback == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_ui_jobs[ctx->slot].callback != 0) {
        return s_ui_jobs[ctx->slot].callback == callback &&
               s_ui_jobs[ctx->slot].user_data == user_data
                   ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_BUSY;
    }
    rc = svc_pump_ensure();
    if (rc != CANOPUS_SVC_OK) return rc;
    s_ui_jobs[ctx->slot].callback = callback;
    s_ui_jobs[ctx->slot].user_data = user_data;
    return CANOPUS_SVC_OK;
}

static void svc_ui_jobs_poll(void)
{
    uint32_t i;
    s_ui_jobs_ready = 0u;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (s_ui_jobs[i].callback != 0) s_ui_jobs_ready |= 1u << i;
    }
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if ((s_ui_jobs_ready & (1u << i)) != 0u && s_ui_jobs[i].callback != 0) {
            canopus_ui_task_fn callback = s_ui_jobs[i].callback;
            void *user_data = s_ui_jobs[i].user_data;
            s_ui_jobs_ready &= ~(1u << i);
            s_ui_jobs[i].callback = 0;
            if (svc_slot_valid(&s_contexts[i])) callback(&s_contexts[i], user_data);
        }
    }
}

/* Fires the timers that are due; a background poll leaves the others alone,
 * because LVGL is suspended while it runs. */
static void svc_timers_dispatch(uint32_t now, int background_only)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_MAX_TIMERS; i++) {
        struct module_timer *t = &s_timers[i];
        if (t->used && t->running && t->cb != 0 && (t->background || !background_only)) {
            if ((int32_t)(now - t->next_fire_ms) >= 0) {
                canopus_timer_callback_t cb = t->cb;
                void *cookie = t->user_data;
                if (t->periodic) {
                    t->next_fire_ms = now + t->interval_ms;
                } else {
                    t->running = 0;
                }
                cb(&s_contexts[t->slot], (int32_t)i, cookie);
            }
        }
    }
}

static uint32_t svc_now(const struct canopus_supervisor_v1 *sup)
{
    return (sup->platform != 0 && sup->platform->monotonic_ms != 0)
               ? sup->platform->monotonic_ms(sup->platform_cookie) : 0u;
}

static void svc_timers_poll(struct canopus_supervisor_v1 *sup, int foreground)
{
    uint32_t now;
    int publish_app;
    if (sup == 0 || sup != s_sup || s_timer_dispatching || s_bus_dispatching) return;
    s_timer_dispatching = 1;
    /* A Launcher update already in flight owns this turn, including its last
     * step. App publication must start on a subsequent foreground heartbeat. */
    publish_app = foreground && !s_launcher_pending;
    /* Only the platform's foreground heartbeat starts a new UI turn. Jobs run
     * before timers, so work posted by those callbacks waits for the next one. */
    if (foreground) svc_ui_jobs_poll();
    now = svc_now(sup);

    canopus_supervisor_stability_poll(sup, now);
    svc_launcher_poll();
    if (publish_app && !s_launcher_pending) canopus_supervisor_native_apps_poll(sup);
    svc_haptic_poll(now);
    svc_events_poll(now);
    svc_timers_dispatch(now, 0);
    s_timer_dispatching = 0;
}

void canopus_supervisor_timers_poll(struct canopus_supervisor_v1 *sup)
{
    svc_timers_poll(sup, 0);
}

static void svc_background_pump(void *sup)
{
    if ((struct canopus_supervisor_v1 *)sup != s_sup || s_timer_dispatching || s_bus_dispatching) return;
    s_timer_dispatching = 1;
    canopus_supervisor_boot_poll(s_sup);
    canopus_supervisor_stability_poll(s_sup, svc_now(s_sup));
    svc_timers_dispatch(svc_now(s_sup), 1);
    s_timer_dispatching = 0;
}

static void svc_timer_pump(void *sup)
{
    svc_timers_poll((struct canopus_supervisor_v1 *)sup, 1);
}

static void svc_time_sleep_ms(const struct canopus_context_v1 *ctx, uint32_t ms)
{
    if (!svc_slot_valid(ctx)) return;
    while (ms != 0u) {
        uint32_t slice = ms > 10u ? 10u : ms;
#if defined(CANOPUS_HAS_NANOSLEEP)
        struct timespec req;
        req.tv_sec = 0;
        req.tv_nsec = (long)(slice * 1000000u);
        while (nanosleep(&req, &req) != 0 && errno == EINTR) {}
#else
        uint32_t start = svc_time_monotonic_ms(ctx);
        while ((uint32_t)(svc_time_monotonic_ms(ctx) - start) < slice) {
            canopus_supervisor_timers_poll(s_sup);
#ifndef CANOPUS_HOST
            __asm__ volatile("yield" ::: "memory");
#endif
        }
#endif
        ms -= slice;
        canopus_supervisor_timers_poll(s_sup);
    }
}

static int32_t svc_time_timer_create(const struct canopus_context_v1 *ctx,
                                     uint32_t interval_ms, int periodic,
                                     canopus_timer_callback_t cb, void *user_data)
{
    uint32_t i;
    if (!svc_slot_valid(ctx) || interval_ms == 0u ||
        interval_ms > CANOPUS_TIMER_MAX_INTERVAL_MS || cb == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (s_sup->platform->start_timer_pump == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    for (i = 0u; i < CANOPUS_MAX_TIMERS; i++) {
        if (!s_timers[i].used) {
            s_timers[i].used = 1;
            s_timers[i].slot = ctx->slot;
            s_timers[i].interval_ms = interval_ms;
            s_timers[i].periodic = periodic;
            s_timers[i].running = 0;
            s_timers[i].cb = cb;
            s_timers[i].user_data = user_data;
            return (int32_t)i;
        }
    }
    return CANOPUS_SVC_ERR_NOMEM;
}

static int32_t svc_time_timer_start(const struct canopus_context_v1 *ctx, int32_t timer_id)
{
    if (!svc_slot_valid(ctx) || timer_id < 0 || timer_id >= (int32_t)CANOPUS_MAX_TIMERS ||
        !s_timers[timer_id].used || s_timers[timer_id].slot != ctx->slot) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    {
        int32_t rc = svc_pump_ensure();
        if (rc != CANOPUS_SVC_OK) return rc;
    }
    s_timers[timer_id].running = 1;
    s_timers[timer_id].next_fire_ms = svc_time_monotonic_ms(ctx) + s_timers[timer_id].interval_ms;
    return CANOPUS_SVC_OK;
}

static int32_t svc_time_timer_stop(const struct canopus_context_v1 *ctx, int32_t timer_id)
{
    if (!svc_slot_valid(ctx) || timer_id < 0 || timer_id >= (int32_t)CANOPUS_MAX_TIMERS ||
        !s_timers[timer_id].used || s_timers[timer_id].slot != ctx->slot) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    s_timers[timer_id].running = 0;
    return CANOPUS_SVC_OK;
}

static int32_t svc_time_timer_destroy(const struct canopus_context_v1 *ctx, int32_t timer_id)
{
    if (!svc_slot_valid(ctx) || timer_id < 0 || timer_id >= (int32_t)CANOPUS_MAX_TIMERS ||
        !s_timers[timer_id].used || s_timers[timer_id].slot != ctx->slot) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    canopus_memset(&s_timers[timer_id], 0, sizeof(s_timers[timer_id]));
    return CANOPUS_SVC_OK;
}

static int32_t svc_background_ensure(void)
{
    const struct canopus_sup_platform_v1 *pf = s_sup->platform;
    int32_t rc;
    if (s_background_pump_started) return CANOPUS_SVC_OK;
    if (pf->start_background_pump == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = svc_errno(pf->start_background_pump(s_sup->platform_cookie, svc_background_pump, s_sup));
    if (rc == CANOPUS_SVC_OK) s_background_pump_started = 1;
    return rc;
}

static int32_t svc_time_timer_set_background(const struct canopus_context_v1 *ctx,
                                            int32_t timer_id, int background)
{
    if (!svc_slot_valid(ctx) || timer_id < 0 || timer_id >= (int32_t)CANOPUS_MAX_TIMERS ||
        !s_timers[timer_id].used || s_timers[timer_id].slot != ctx->slot) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (background) {
        int32_t rc = svc_background_ensure();
        if (rc != CANOPUS_SVC_OK) return rc;
    }
    s_timers[timer_id].background = background != 0;
    return CANOPUS_SVC_OK;
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

static int svc_hook_is_hooked(const struct canopus_context_v1 *ctx, uintptr_t target_fn)
{
    (void)ctx;
    return canopus_hook_is_hooked(target_fn);
}

static int32_t svc_hook_register_slot_map(const struct canopus_context_v1 *ctx,
                                         uintptr_t flash_fn, uintptr_t ram_slot)
{
    (void)ctx;
    return canopus_hook_register_slot_map(ctx->slot, flash_fn, ram_slot);
}

/* ---- HAPTICS (Vibration & Tactile Presets) --------------------------- */

static int32_t svc_pump_ensure(void)
{
    int rc;
    if (s_timer_pump_started) return CANOPUS_SVC_OK;
    if (s_sup->platform->start_timer_pump == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = s_sup->platform->start_timer_pump(s_sup->platform_cookie, svc_timer_pump, s_sup);
    if (rc != 0) return svc_errno(rc);
    s_timer_pump_started = 1;
    return CANOPUS_SVC_OK;
}

int canopus_supervisor_start_background(struct canopus_supervisor_v1 *sup)
{
    return sup == s_sup ? svc_background_ensure() : CANOPUS_SVC_ERR_STATE;
}

int canopus_supervisor_watch_stability(struct canopus_supervisor_v1 *sup)
{
    int32_t rc = CANOPUS_SVC_OK;
    int started = 0;
    if (sup != s_sup) return CANOPUS_SVC_ERR_STATE;
    if (sup->platform->start_background_pump != 0) {
        rc = svc_background_ensure();
        if (rc != CANOPUS_SVC_OK) return rc;
        started = 1;
    }
    if (sup->platform->start_timer_pump != 0) {
        rc = svc_pump_ensure();
        if (rc != CANOPUS_SVC_OK) return rc;
        started = 1;
    }
    return started ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_UNSUPPORTED;
}

static int svc_haptics_ready(const struct canopus_context_v1 *ctx)
{
    return svc_slot_valid(ctx) && s_sup->platform != 0 && s_sup->platform->vibrate != 0;
}

static void svc_haptic_stop(void)
{
    if (s_haptic.active) {
        if (s_sup->platform->vibrate_cancel != 0) {
            (void)s_sup->platform->vibrate_cancel(s_sup->platform_cookie);
        }
        s_haptic.active = 0u;
    }
}

/* Plays the schedule up to `now`. Steps alternate pulse (even) and gap (odd)
 * and keep their scheduled times even when the pump is late: a late pulse is
 * shortened to what is left of its slot, a slot that has fully elapsed is
 * skipped. */
static int32_t svc_haptic_run(uint32_t now)
{
    while (s_haptic.active) {
        uint32_t end = s_haptic.next + s_haptic.pattern[s_haptic.step];
        if (!s_haptic.started) {
            if ((int32_t)(now - s_haptic.next) < 0) return CANOPUS_SVC_OK;
            s_haptic.started = 1u;
            if ((s_haptic.step & 1u) == 0u && (int32_t)(end - now) > 0) {
                int32_t rc = svc_errno(s_sup->platform->vibrate(s_sup->platform_cookie,
                                                                end - now));
                if (rc < 0) {
                    s_haptic.active = 0u;
                    return rc;
                }
            }
        }
        if ((int32_t)(now - end) < 0) return CANOPUS_SVC_OK;
        s_haptic.next = end;
        s_haptic.started = 0u;
        if (++s_haptic.step >= s_haptic.count) s_haptic.active = 0u;
    }
    return CANOPUS_SVC_OK;
}

static void svc_haptic_poll(uint32_t now)
{
    if (s_haptic.active) (void)svc_haptic_run(now);
}

static int32_t svc_haptic_play(const struct canopus_context_v1 *ctx,
                               const uint16_t *pattern, uint32_t count)
{
    uint32_t i, total = 0u;
    int32_t rc;
    if (!svc_haptics_ready(ctx)) {
        return svc_slot_valid(ctx) ? CANOPUS_SVC_ERR_UNSUPPORTED : CANOPUS_SVC_ERR_STATE;
    }
    if (pattern == 0 || count == 0u || count > CANOPUS_HAPTIC_PATTERN_MAX) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    for (i = 0u; i < count; i++) {
        if (pattern[i] > CANOPUS_HAPTIC_MAX_MS) return CANOPUS_SVC_ERR_ARGUMENT;
        total += pattern[i];
    }
    if (total == 0u) return CANOPUS_SVC_ERR_ARGUMENT;
    if (count > 1u) {
        rc = svc_pump_ensure();
        if (rc != CANOPUS_SVC_OK) return rc;
    }
    svc_haptic_stop();
    for (i = 0u; i < count; i++) s_haptic.pattern[i] = pattern[i];
    s_haptic.count = (uint8_t)count;
    s_haptic.step = 0u;
    s_haptic.started = 0u;
    s_haptic.slot = (uint8_t)ctx->slot;
    s_haptic.next = svc_time_monotonic_ms(ctx);
    s_haptic.active = 1u;
    return svc_haptic_run(s_haptic.next);
}

static int32_t svc_haptics_vibrate(const struct canopus_context_v1 *ctx, uint32_t duration_ms)
{
    uint16_t pulse;
    if (duration_ms == 0u || duration_ms > CANOPUS_HAPTIC_MAX_MS) {
        return svc_slot_valid(ctx) ? CANOPUS_SVC_ERR_ARGUMENT : CANOPUS_SVC_ERR_STATE;
    }
    pulse = (uint16_t)duration_ms;
    return svc_haptic_play(ctx, &pulse, 1u);
}

static int32_t svc_haptics_vibrate_pattern(const struct canopus_context_v1 *ctx,
                                           const uint16_t *pattern_ms, uint32_t count)
{
    return svc_haptic_play(ctx, pattern_ms, count);
}

static int32_t svc_haptics_vibrate_preset(const struct canopus_context_v1 *ctx, uint32_t preset)
{
    static const uint16_t click[] = {20u};
    static const uint16_t double_click[] = {20u, 60u, 20u};
    static const uint16_t heavy[] = {100u};
    static const uint16_t alert[] = {250u};
    static const uint16_t tick[] = {10u};
    switch (preset) {
    case CANOPUS_HAPTIC_CLICK: return svc_haptic_play(ctx, click, 1u);
    case CANOPUS_HAPTIC_DOUBLE_CLICK: return svc_haptic_play(ctx, double_click, 3u);
    case CANOPUS_HAPTIC_HEAVY: return svc_haptic_play(ctx, heavy, 1u);
    case CANOPUS_HAPTIC_ALERT: return svc_haptic_play(ctx, alert, 1u);
    case CANOPUS_HAPTIC_TICK: return svc_haptic_play(ctx, tick, 1u);
    default: return CANOPUS_SVC_ERR_ARGUMENT;
    }
}

/* Stops the caller's own vibration; there is nothing to stop otherwise. */
static int32_t svc_haptics_cancel(const struct canopus_context_v1 *ctx)
{
    if (!svc_haptics_ready(ctx)) {
        return svc_slot_valid(ctx) ? CANOPUS_SVC_ERR_UNSUPPORTED : CANOPUS_SVC_ERR_STATE;
    }
    if (s_haptic.active && s_haptic.slot == ctx->slot) svc_haptic_stop();
    return CANOPUS_SVC_OK;
}

/* ---- DISPLAY & WAKE LOCKS -------------------------------------------- */

static uint32_t svc_display_get_brightness(const struct canopus_context_v1 *ctx)
{
    uint32_t percent;
    (void)ctx;
    if (s_brightness_slot >= 0) return s_brightness_percent;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->get_brightness == 0 ||
        s_sup->platform->get_brightness(s_sup->platform_cookie, &percent) != 0) {
        return CANOPUS_DISPLAY_BRIGHTNESS_UNKNOWN;
    }
    return percent;
}

static int32_t svc_display_set_brightness(const struct canopus_context_v1 *ctx, uint32_t percent)
{
    int32_t rc;
    if (!svc_slot_valid(ctx) || percent > 100u) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_sup->platform == 0 || s_sup->platform->set_brightness == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = svc_errno(s_sup->platform->set_brightness(s_sup->platform_cookie, percent));
    if (rc < 0) return rc;
    s_brightness_slot = (int32_t)ctx->slot;
    s_brightness_percent = percent;
    return CANOPUS_SVC_OK;
}

static void svc_brightness_release(uint32_t slot)
{
    if (s_brightness_slot == (int32_t)slot) {
        if (s_sup->platform != 0 && s_sup->platform->restore_brightness != 0) {
            (void)s_sup->platform->restore_brightness(s_sup->platform_cookie);
        }
        s_brightness_slot = -1;
    }
}

/* 1 only while the screen is on; the always-on display and off are 0. */
static int svc_display_is_screen_on(const struct canopus_context_v1 *ctx)
{
    int state;
    (void)ctx;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->screen_state == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    state = s_sup->platform->screen_state(s_sup->platform_cookie);
    return state < 0 ? (int)svc_errno(state) : state == (int)CANOPUS_SCREEN_ON;
}

static int32_t svc_display_wake_screen(const struct canopus_context_v1 *ctx)
{
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform == 0 || s_sup->platform->wake_screen == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    return svc_errno(s_sup->platform->wake_screen(s_sup->platform_cookie));
}

static int32_t svc_display_acquire_wake_lock(const struct canopus_context_v1 *ctx, uint32_t lock_flags)
{
    uint32_t i, owned = 0u, free_index = CANOPUS_WAKELOCK_MAX;
    int32_t rc;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (lock_flags != CANOPUS_WAKELOCK_SCREEN) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_sup->platform == 0 || s_sup->platform->wakelock_acquire == 0 ||
        s_sup->platform->wakelock_release == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    for (i = 0u; i < CANOPUS_WAKELOCK_MAX; i++) {
        if (!s_wakelocks[i].used) {
            if (free_index == CANOPUS_WAKELOCK_MAX) free_index = i;
        } else if (s_wakelocks[i].slot == ctx->slot) {
            owned++;
        }
    }
    if (owned >= CANOPUS_WAKELOCK_PER_MODULE || free_index == CANOPUS_WAKELOCK_MAX) {
        return CANOPUS_SVC_ERR_BUSY;
    }
    rc = svc_errno(s_sup->platform->wakelock_acquire(s_sup->platform_cookie, free_index));
    if (rc < 0) return rc;
    s_wakelocks[free_index].used = 1u;
    s_wakelocks[free_index].slot = (uint8_t)ctx->slot;
    s_wakelocks[free_index].generation = ++s_wakelock_generation;
    /* id = generation * MAX + index + 1: unique until the generation wraps. */
    return (int32_t)(((uint32_t)(s_wakelocks[free_index].generation & 0x3FFFu) *
                      CANOPUS_WAKELOCK_MAX) + free_index + 1u);
}

static int32_t svc_display_release_wake_lock(const struct canopus_context_v1 *ctx, int32_t lock_id)
{
    uint32_t index, generation;
    int32_t rc;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (lock_id <= 0) return CANOPUS_SVC_ERR_ARGUMENT;
    index = ((uint32_t)lock_id - 1u) % CANOPUS_WAKELOCK_MAX;
    generation = ((uint32_t)lock_id - 1u) / CANOPUS_WAKELOCK_MAX;
    if (!s_wakelocks[index].used || s_wakelocks[index].slot != ctx->slot ||
        (uint32_t)(s_wakelocks[index].generation & 0x3FFFu) != generation) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    rc = svc_errno(s_sup->platform->wakelock_release(s_sup->platform_cookie, index));
    if (rc < 0) return rc;
    s_wakelocks[index].used = 0u;
    return CANOPUS_SVC_OK;
}

static void svc_wakelocks_release(uint32_t slot)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_WAKELOCK_MAX; i++) {
        if (s_wakelocks[i].used && s_wakelocks[i].slot == slot) {
            s_wakelocks[i].used = 0u;
            if (s_sup->platform != 0 && s_sup->platform->wakelock_release != 0) {
                (void)s_sup->platform->wakelock_release(s_sup->platform_cookie, i);
            }
        }
    }
}

static void svc_display_get_resolution(const struct canopus_context_v1 *ctx, uint32_t *width, uint32_t *height)
{
    const char *target = (ctx != 0 && ctx->target_id != 0) ? ctx->target_id : "";
    if (width == 0 || height == 0) return;
    if (target[12] == '1' && target[13] == '1') {
        /* Band 11: 212 x 520 */
        *width = 212u;
        *height = 520u;
    } else {
        /* Band 10 Pro: 336 x 480 */
        *width = 336u;
        *height = 480u;
    }
}

/* ---- BUS (Inter-Module Event Bus) ------------------------------------ */

static int svc_bus_is_sampled_topic(const char *topic)
{
    return canopus_strcmp(topic, CANOPUS_BUS_TOPIC_SCREEN) == 0 ||
           canopus_strcmp(topic, CANOPUS_BUS_TOPIC_BATTERY) == 0 ||
           canopus_strcmp(topic, CANOPUS_BUS_TOPIC_BT) == 0;
}

static int32_t svc_bus_dispatch(const char *topic, const void *payload, uint32_t len)
{
    uint32_t i;
    int32_t dispatched = 0;
    if (s_bus_dispatching) return CANOPUS_SVC_ERR_BUSY;
    s_bus_dispatching = 1;
    for (i = 0u; i < CANOPUS_BUS_MAX_SUBS; i++) {
        if (s_bus_subs[i].used && s_bus_subs[i].cb != 0) {
            if (s_bus_subs[i].topic[0] == '*' || canopus_strcmp(s_bus_subs[i].topic, topic) == 0) {
                const struct canopus_context_v1 *sub_ctx = &s_contexts[s_bus_subs[i].slot];
                s_bus_subs[i].cb(sub_ctx, topic, payload, len, s_bus_subs[i].user_data);
                dispatched++;
            }
        }
    }
    s_bus_dispatching = 0;
    return dispatched;
}

static int32_t svc_bus_query_module(const struct canopus_context_v1 *ctx,
                                    const char *id, struct canopus_module_state_v1 *out)
{
    uint32_t i;
    if (!svc_slot_valid(ctx) || id == 0 || out == 0 || id[0] == '\0' ||
        canopus_strnlen(id, CANOPUS_MODULE_ID_MAX) == CANOPUS_MODULE_ID_MAX) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        const struct canopus_sup_module_v1 *m = &s_sup->modules[i];
        if (m->state != 0u && canopus_strcmp((const char *)m->module_id, id) == 0) {
            canopus_memcpy(out->module_id, m->module_id, sizeof(out->module_id));
            out->state = m->state;
            out->intent = m->intent;
            out->version = m->version;
            out->lifecycle_class = m->lifecycle_class;
            out->flags = m->flags;
            out->activation_error = (int32_t)m->activation_error;
            out->active_generation = m->active_gen;
            return CANOPUS_SVC_OK;
        }
    }
    return CANOPUS_SVC_ERR_NOTFOUND;
}

int canopus_supervisor_bus_dispatching(void)
{
    return s_bus_dispatching;
}

void canopus_supervisor_module_changed(struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    if (sup == s_sup && slot < CANOPUS_SUP_MODULE_SLOTS) s_module_events |= 1u << slot;
}

void canopus_supervisor_module_events_flush(struct canopus_supervisor_v1 *sup)
{
    uint32_t i, pending;
    struct canopus_module_state_v1 event;
    if (sup != s_sup || s_bus_dispatching || !CANOPUS_SNAPSHOT_READY(&sup->snap)) return;
    pending = s_module_events;
    s_module_events = 0u;
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if ((pending & (1u << i)) == 0u || sup->modules[i].state == 0u) continue;
        if (svc_bus_query_module(&s_contexts[i], (const char *)sup->modules[i].module_id,
                                 &event) == 0) {
            (void)svc_bus_dispatch(CANOPUS_BUS_TOPIC_MODULE, &event, sizeof(event));
        }
    }
}

/* Reserved system topics cannot be forged by a module. */
static int32_t svc_bus_publish(const struct canopus_context_v1 *ctx, const char *topic,
                               const void *payload, uint32_t len)
{
    if (!svc_slot_valid(ctx) || topic == 0 || topic[0] == '\0' ||
        (len != 0u && payload == 0) || canopus_strnlen(topic, 32u) >= 32u) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (svc_bus_is_sampled_topic(topic) || canopus_strcmp(topic, CANOPUS_BUS_TOPIC_MODULE) == 0) {
        return CANOPUS_SVC_ERR_ACCESS;
    }
    return svc_bus_dispatch(topic, payload, len);
}

/* Subscribed system topics start sampling the device; a new subscriber
 * invalidates the last-published values so it receives the current state. */
static uint32_t svc_events_wanted(void)
{
    uint32_t i, wanted = 0u;
    for (i = 0u; i < CANOPUS_BUS_MAX_SUBS; i++) {
        if (!s_bus_subs[i].used || s_bus_subs[i].cb == 0) continue;
        if (s_bus_subs[i].topic[0] == '*') {
            wanted |= SVC_EVENT_SCREEN | SVC_EVENT_BATTERY | SVC_EVENT_BT;
        } else if (canopus_strcmp(s_bus_subs[i].topic, CANOPUS_BUS_TOPIC_SCREEN) == 0) {
            wanted |= SVC_EVENT_SCREEN;
        } else if (canopus_strcmp(s_bus_subs[i].topic, CANOPUS_BUS_TOPIC_BATTERY) == 0) {
            wanted |= SVC_EVENT_BATTERY;
        } else if (canopus_strcmp(s_bus_subs[i].topic, CANOPUS_BUS_TOPIC_BT) == 0) {
            wanted |= SVC_EVENT_BT;
        }
    }
    return wanted;
}

static void svc_events_refresh(void)
{
    s_events.wanted = svc_events_wanted();
    s_events.known = 0u;
    s_events.next_ms = 0u;
}

static void svc_events_poll(uint32_t now)
{
    uint8_t payload[2];
    int state, rc;
    uint32_t percent, adapter;
    int charging;
    if (s_events.wanted == 0u || s_sup->platform == 0 ||
        (int32_t)(now - s_events.next_ms) < 0) {
        return;
    }
    s_events.next_ms = now + SVC_EVENT_PERIOD_MS;
    if ((s_events.wanted & SVC_EVENT_SCREEN) != 0u && s_sup->platform->screen_state != 0) {
        state = s_sup->platform->screen_state(s_sup->platform_cookie);
        if (state >= 0 && ((s_events.known & SVC_EVENT_SCREEN) == 0u || s_events.screen != state)) {
            s_events.known |= SVC_EVENT_SCREEN;
            s_events.screen = (uint8_t)state;
            payload[0] = (uint8_t)state;
            (void)svc_bus_dispatch(CANOPUS_BUS_TOPIC_SCREEN, payload, 1u);
        }
    }
    if ((s_events.wanted & SVC_EVENT_BATTERY) != 0u && s_sup->platform->battery_state != 0 &&
        s_sup->platform->battery_state(s_sup->platform_cookie, &percent, &charging) == 0 &&
        percent <= 100u) {
        payload[0] = (uint8_t)percent;
        payload[1] = charging ? 1u : 0u;
        if ((s_events.known & SVC_EVENT_BATTERY) == 0u || s_events.battery[0] != payload[0] ||
            s_events.battery[1] != payload[1]) {
            s_events.known |= SVC_EVENT_BATTERY;
            s_events.battery[0] = payload[0];
            s_events.battery[1] = payload[1];
            (void)svc_bus_dispatch(CANOPUS_BUS_TOPIC_BATTERY, payload, 2u);
        }
    }
    if ((s_events.wanted & SVC_EVENT_BT) != 0u && s_sup->platform->bt_adapter_state != 0 &&
        s_sup->platform->bt_adapter_state(s_sup->platform_cookie, &adapter) == 0) {
        rc = s_sup->platform->bt_is_connected != 0
                 ? s_sup->platform->bt_is_connected(s_sup->platform_cookie) : -1;
        payload[0] = (uint8_t)adapter;
        payload[1] = rc < 0 ? 0xFFu : (rc != 0 ? 1u : 0u);
        if ((s_events.known & SVC_EVENT_BT) == 0u || s_events.bt[0] != payload[0] ||
            s_events.bt[1] != payload[1]) {
            s_events.known |= SVC_EVENT_BT;
            s_events.bt[0] = payload[0];
            s_events.bt[1] = payload[1];
            (void)svc_bus_dispatch(CANOPUS_BUS_TOPIC_BT, payload, 2u);
        }
    }
}

static int32_t svc_bus_subscribe(const struct canopus_context_v1 *ctx, const char *topic,
                                 canopus_bus_callback_t cb, void *user_data)
{
    uint32_t i;
    if (!svc_slot_valid(ctx) || topic == 0 || cb == 0 || topic[0] == '\0' ||
        canopus_strnlen(topic, 32u) >= 32u || (topic[0] == '*' && topic[1] != '\0')) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (s_bus_dispatching) return CANOPUS_SVC_ERR_BUSY;
    for (i = 0u; i < CANOPUS_BUS_MAX_SUBS; i++) {
        if (!s_bus_subs[i].used) {
            s_bus_subs[i].used = 1;
            s_bus_subs[i].slot = ctx->slot;
            s_bus_subs[i].cb = cb;
            s_bus_subs[i].user_data = user_data;
            canopus_strncpy(s_bus_subs[i].topic, topic, sizeof(s_bus_subs[i].topic) - 1);
            s_bus_subs[i].topic[sizeof(s_bus_subs[i].topic) - 1] = '\0';
            if (svc_bus_is_sampled_topic(s_bus_subs[i].topic) &&
                svc_pump_ensure() != CANOPUS_SVC_OK) {
                /* System events come from the timer pump; without one the
                 * subscription would silently never fire. */
                canopus_memset(&s_bus_subs[i], 0, sizeof(s_bus_subs[i]));
                return CANOPUS_SVC_ERR_UNSUPPORTED;
            }
            if (s_bus_subs[i].topic[0] == '*') (void)svc_pump_ensure();
            svc_events_refresh();
            return (int32_t)i;
        }
    }
    return CANOPUS_SVC_ERR_NOMEM;
}

static int32_t svc_bus_unsubscribe(const struct canopus_context_v1 *ctx, int32_t sub_id)
{
    if (!svc_slot_valid(ctx) || sub_id < 0 || sub_id >= (int32_t)CANOPUS_BUS_MAX_SUBS ||
        !s_bus_subs[sub_id].used || s_bus_subs[sub_id].slot != ctx->slot) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    canopus_memset(&s_bus_subs[sub_id], 0, sizeof(s_bus_subs[sub_id]));
    s_events.wanted = svc_events_wanted();
    return CANOPUS_SVC_OK;
}

/* ---- NOTIFY ---------------------------------------------------------- */

static void svc_copy_bounded(char *out, uint32_t cap, const char *text)
{
    uint32_t n = 0u;
    while (n < cap && text[n] != '\0') {
        out[n] = text[n];
        n++;
    }
    out[n] = '\0';
}

/* The module id is always the notification's source; a missing or empty title
 * shows the id as well. `icon` is a resolved absolute path or NULL. */
static int32_t svc_notify_send(const struct canopus_context_v1 *ctx, const char *title,
                               const char *body, const char *icon)
{
    char text[CANOPUS_NOTIFY_BODY_MAX + 1u];
    char head[CANOPUS_NOTIFY_TITLE_MAX + 1u];
    const char *source;
    int rc;
    if (!svc_slot_valid(ctx) || body == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_notify_posts[ctx->slot] >= CANOPUS_NOTIFY_MAX_POSTS) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform == 0 || s_sup->platform->notify == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    source = (const char *)s_sup->modules[ctx->slot].module_id;
    svc_copy_bounded(text, CANOPUS_NOTIFY_BODY_MAX, body);
    svc_copy_bounded(head, CANOPUS_NOTIFY_TITLE_MAX,
                     title != 0 && title[0] != '\0' ? title : source);
    rc = s_sup->platform->notify(s_sup->platform_cookie, source, head, text, icon);
    if (rc != 0) return svc_errno(rc < 0 ? rc : CANOPUS_SVC_ERR_IO);
    s_notify_posts[ctx->slot]++;
    return CANOPUS_SVC_OK;
}

static int32_t svc_notify_post(const struct canopus_context_v1 *ctx, const char *body)
{
    return svc_notify_send(ctx, 0, body, 0);
}

static int32_t svc_notify_post_obj(const struct canopus_context_v1 *ctx,
                                   const char *title, const char *body)
{
    return svc_notify_send(ctx, title, body, 0);
}

static int32_t svc_notify_post_with_icon(const struct canopus_context_v1 *ctx,
                                        const char *title, const char *body,
                                        const char *icon_path, int vibrate)
{
    struct svc_fs_path icon;
    struct canopus_fs_stat st;
    int32_t rc;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (icon_path != 0 && icon_path[0] != '\0') {
        if (icon_path[0] != '/') return CANOPUS_SVC_ERR_ARGUMENT;
        rc = svc_fs_stat(ctx, icon_path, &st);
        if (rc != CANOPUS_SVC_OK) return rc;
        if (st.is_dir) return CANOPUS_SVC_ERR_ISDIR;
        (void)svc_fs_resolve(ctx, icon_path, &icon);
    } else {
        icon.path[0] = '\0';
    }
    rc = svc_notify_send(ctx, title, body, icon.path[0] != '\0' ? icon.path : 0);
    if (rc != CANOPUS_SVC_OK || !vibrate) return rc;
    return svc_haptics_vibrate(ctx, 100u) == CANOPUS_SVC_OK ? CANOPUS_SVC_OK
                                                            : CANOPUS_NOTIFY_HAPTIC_SKIPPED;
}

/* ---- SYSTEM ---------------------------------------------------------- */

static const char *svc_system_target_id(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return (s_sup != 0 && s_sup->platform != 0) ? s_sup->platform->target_id : "";
}

static const char *svc_system_module_id(const struct canopus_context_v1 *ctx)
{
    return (ctx != 0 && ctx->module_id != 0) ? ctx->module_id : "";
}

static const char *svc_system_device_model(const struct canopus_context_v1 *ctx)
{
    const char *target = svc_system_target_id(ctx);
    if (target[12] == '1' && target[13] == '1') {
        return "Xiaomi Band 11";
    }
    return "Xiaomi Band 10 Pro";
}

static const char *svc_system_firmware_version(const struct canopus_context_v1 *ctx)
{
    const char *target = svc_system_target_id(ctx);
    uint32_t len = 0u;
    while (target[len] != '\0') len++;
    while (len > 0u) {
        len--;
        if (target[len] == '-' && target[len + 1u] >= '0' && target[len + 1u] <= '9') {
            return &target[len + 1u];
        }
    }
    return target;
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

static uint64_t svc_system_uptime_ms(const struct canopus_context_v1 *ctx)
{
    return (uint64_t)svc_time_monotonic_ms(ctx);
}

static uint32_t svc_system_battery_level(const struct canopus_context_v1 *ctx)
{
    uint32_t percent;
    int charging;
    (void)ctx;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->battery_state == 0 ||
        s_sup->platform->battery_state(s_sup->platform_cookie, &percent, &charging) != 0 ||
        percent > 100u) {
        return CANOPUS_SYSTEM_BATTERY_UNKNOWN;
    }
    return percent;
}

static int svc_system_is_charging(const struct canopus_context_v1 *ctx)
{
    uint32_t percent;
    int charging, rc;
    (void)ctx;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->battery_state == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = s_sup->platform->battery_state(s_sup->platform_cookie, &percent, &charging);
    if (rc != 0) return (int)svc_errno(rc < 0 ? rc : CANOPUS_SVC_ERR_IO);
    return charging ? 1 : 0;
}

static int32_t svc_system_reboot(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    if (s_sup != 0 && s_sup->platform != 0 && s_sup->platform->reboot != 0) {
        return s_sup->platform->reboot(s_sup->platform_cookie);
    }
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}

static int svc_property_key_ok(const char *key)
{
    uint32_t n = 0u;
    while (key[n] != '\0') {
        char c = key[n];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '_' || c == '-')) return 0;
        if (++n > 63u) return 0;
    }
    return n != 0u;
}

/* Returns the property's length (truncated to buffer - 1), NOTFOUND when the
 * key is absent, UNSUPPORTED when this target has no property reader. */
static int32_t svc_system_get_property(const struct canopus_context_v1 *ctx,
                                       const char *key, char *buf, uint32_t max_len)
{
    int rc;
    (void)ctx;
    if (key == 0 || buf == 0 || max_len == 0u || !svc_property_key_ok(key)) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->get_property == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = s_sup->platform->get_property(s_sup->platform_cookie, key, buf, max_len);
    return rc < 0 ? svc_errno(rc) : rc;
}

/* ---- BLUETOOTH & L2 -------------------------------------------------- */

struct svc_l2_rx {
    canopus_l2_rx_callback_t cb;
    void *user_data;
    uint8_t slot;
};
static struct svc_l2_rx s_l2_rx[CANOPUS_BT_MODULE_CHANNEL_LAST - CANOPUS_BT_MODULE_CHANNEL_FIRST + 1u];

static int32_t svc_bt_l2_send(const struct canopus_context_v1 *ctx, uint8_t channel,
                              const void *payload, uint16_t len)
{
    (void)ctx;
    if (channel == CANOPUS_L2_CHANNEL) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_sup != 0 && s_sup->platform != 0 && s_sup->platform->l2_send != 0) {
        return s_sup->platform->l2_send(s_sup->platform_cookie, channel, payload, len);
    }
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}

static int svc_l2_rx_channel_ok(uint8_t channel)
{
    return channel >= CANOPUS_BT_MODULE_CHANNEL_FIRST &&
           channel <= CANOPUS_BT_MODULE_CHANNEL_LAST && channel != 0x43u;
}

/* Registration is recorded only where the L2 backend can deliver; the backend
 * routes a registered channel's frames here (canopus_supervisor_l2_rx_*). */
static int32_t svc_bt_l2_register_rx(const struct canopus_context_v1 *ctx, uint8_t channel,
                                     canopus_l2_rx_callback_t cb, void *user_data)
{
    struct svc_l2_rx *entry;
    if (!svc_slot_valid(ctx) || !svc_l2_rx_channel_ok(channel)) return CANOPUS_SVC_ERR_ARGUMENT;
    if (s_sup->platform == 0 || s_sup->platform->l2_send == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    entry = &s_l2_rx[channel - CANOPUS_BT_MODULE_CHANNEL_FIRST];
    if (entry->cb != 0 && entry->slot != ctx->slot) return CANOPUS_SVC_ERR_BUSY;
    if (cb == 0) {
        if (entry->cb != 0) canopus_memset(entry, 0, sizeof(*entry));
        return CANOPUS_SVC_OK;
    }
    entry->cb = cb;
    entry->user_data = user_data;
    entry->slot = (uint8_t)ctx->slot;
    return CANOPUS_SVC_OK;
}

int canopus_supervisor_l2_rx_registered(uint8_t channel)
{
    return svc_l2_rx_channel_ok(channel) &&
           s_l2_rx[channel - CANOPUS_BT_MODULE_CHANNEL_FIRST].cb != 0;
}

/* Runs on the module/UI task. Returns 1 when a live module took the frame. */
int canopus_supervisor_l2_rx_dispatch(struct canopus_supervisor_v1 *sup, uint8_t channel,
                                      const void *payload, uint16_t len)
{
    const struct svc_l2_rx *entry;
    if (sup == 0 || sup != s_sup || !canopus_supervisor_l2_rx_registered(channel)) return 0;
    entry = &s_l2_rx[channel - CANOPUS_BT_MODULE_CHANNEL_FIRST];
    if (sup->modules[entry->slot].state == 0u) return 0;
    entry->cb(&s_contexts[entry->slot], channel, payload, len, entry->user_data);
    return 1;
}

static int svc_bt_is_connected(const struct canopus_context_v1 *ctx)
{
    int rc;
    (void)ctx;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->bt_is_connected == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    rc = s_sup->platform->bt_is_connected(s_sup->platform_cookie);
    return rc < 0 ? (int)svc_errno(rc) : (rc != 0 ? 1 : 0);
}

static uint32_t svc_bt_adapter_state(const struct canopus_context_v1 *ctx)
{
    uint32_t state;
    (void)ctx;
    if (s_sup == 0 || s_sup->platform == 0 || s_sup->platform->bt_adapter_state == 0 ||
        s_sup->platform->bt_adapter_state(s_sup->platform_cookie, &state) != 0) {
        return CANOPUS_BT_ADAPTER_UNKNOWN;
    }
    return state;
}

/* ---- UI page entries (ADR-CAN-010) ------------------------------------ */

static int32_t svc_ui_set_entry(const struct canopus_context_v1 *ctx,
                                const struct canopus_ui_entry_v1 *entry)
{
    uint32_t len = 0u;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform == 0 || s_sup->platform->ui_page_close == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    if (entry == 0) {
        canopus_memset(&s_ui_entries[ctx->slot], 0, sizeof(s_ui_entries[ctx->slot]));
        (void)s_sup->platform->ui_page_close(s_sup->platform_cookie, ctx->slot);
        return CANOPUS_SVC_OK;
    }
    if (entry->struct_size != sizeof(*entry) || entry->on_open == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (entry->label != 0) {
        len = (uint32_t)canopus_strnlen(entry->label, CANOPUS_UI_ENTRY_LABEL_MAX);
        if (len >= CANOPUS_UI_ENTRY_LABEL_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
    }
    canopus_memset(&s_ui_entries[ctx->slot], 0, sizeof(s_ui_entries[ctx->slot]));
    s_ui_entries[ctx->slot].entry = *entry;
    if (len != 0u) canopus_memcpy(s_ui_entries[ctx->slot].label, entry->label, len);
    s_ui_entries[ctx->slot].entry.label = s_ui_entries[ctx->slot].label;
    s_ui_entries[ctx->slot].used = 1u;
    return CANOPUS_SVC_OK;
}

static int32_t svc_ui_close_page(const struct canopus_context_v1 *ctx)
{
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (s_sup->platform == 0 || s_sup->platform->ui_page_close == 0) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    return s_sup->platform->ui_page_close(s_sup->platform_cookie, ctx->slot)
               ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_STATE;
}

int canopus_supervisor_ui_entry(struct canopus_supervisor_v1 *sup, uint32_t slot,
                                struct canopus_ui_entry_v1 *out)
{
    if (sup == 0 || sup != s_sup || out == 0 || slot >= CANOPUS_SUP_MODULE_SLOTS ||
        !s_ui_entries[slot].used || sup->modules[slot].state == 0u) {
        return -1;
    }
    *out = s_ui_entries[slot].entry;
    return 0;
}

/* ---- launcher entries (ADR-CAN-011) ---------------------------------- */

static int32_t svc_launcher_set_entry(const struct canopus_context_v1 *ctx,
                                      const struct canopus_launcher_entry_v1 *entry)
{
    const struct canopus_sup_platform_v1 *pf = s_sup->platform;
    uint32_t label_len, icon_len = 0u;
    int32_t rc;
    if (!svc_slot_valid(ctx)) return CANOPUS_SVC_ERR_STATE;
    if (entry == 0 || entry->struct_size != sizeof(*entry) || entry->label == 0) {
        return CANOPUS_SVC_ERR_ARGUMENT;
    }
    label_len = (uint32_t)canopus_strnlen(entry->label, CANOPUS_LAUNCHER_LABEL_MAX);
    if (label_len == 0u || label_len >= CANOPUS_LAUNCHER_LABEL_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
    if (entry->icon != 0) {
        icon_len = (uint32_t)canopus_strnlen(entry->icon, CANOPUS_LAUNCHER_ICON_MAX);
        if (icon_len == 0u || icon_len >= CANOPUS_LAUNCHER_ICON_MAX) return CANOPUS_SVC_ERR_ARGUMENT;
    }
    if (!s_ui_entries[ctx->slot].used) return CANOPUS_SVC_ERR_STATE;
    if (s_launcher[ctx->slot].used) {
        return canopus_strcmp(s_launcher[ctx->slot].label, entry->label) == 0 &&
               canopus_strcmp(s_launcher[ctx->slot].icon, entry->icon != 0 ? entry->icon : "") == 0
                   ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_BUSY;
    }
    if (pf->launcher_publish == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = svc_pump_ensure();
    if (rc != CANOPUS_SVC_OK) return rc;
    rc = svc_errno(pf->launcher_publish(s_sup->platform_cookie, ctx->slot, entry->label, entry->icon));
    if (rc != CANOPUS_SVC_OK) return rc;
    canopus_memcpy(s_launcher[ctx->slot].label, entry->label, label_len);
    if (icon_len != 0u) canopus_memcpy(s_launcher[ctx->slot].icon, entry->icon, icon_len);
    s_launcher[ctx->slot].used = 1u;
    s_launcher_pending = 1u;
    return CANOPUS_SVC_OK;
}

/* One firmware call per heartbeat tick, so each runs on its own UI event turn. */
static void svc_launcher_poll(void)
{
    int rc;
    if (!s_launcher_pending) return;
    rc = s_sup->platform->launcher_step(s_sup->platform_cookie);
    if (rc <= 0) s_launcher_pending = 0u;
    if (rc < 0) s_sup->error_code = (uint32_t)CANOPUS_SUP_ERR_LAUNCHER;
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
    canopus_memset(s_timers, 0, sizeof(s_timers));
    s_timer_dispatching = 0;
    s_timer_pump_started = 0;
    s_background_pump_started = 0;
    canopus_memset(s_bus_subs, 0, sizeof(s_bus_subs));
    s_bus_dispatching = 0;
    s_module_events = 0u;
    canopus_memset(s_ui_jobs, 0, sizeof(s_ui_jobs));
    s_ui_jobs_ready = 0u;
    canopus_memset(s_wakelocks, 0, sizeof(s_wakelocks));
    s_wakelock_generation = 0u;
    s_brightness_slot = -1;
    s_brightness_percent = 0u;
    canopus_memset(&s_haptic, 0, sizeof(s_haptic));
    canopus_memset(&s_events, 0, sizeof(s_events));
    canopus_memset(s_l2_rx, 0, sizeof(s_l2_rx));
    canopus_memset(s_ui_entries, 0, sizeof(s_ui_entries));
    canopus_memset(s_launcher, 0, sizeof(s_launcher));
    s_launcher_pending = 0u;
    s_connect_obj = canopus_supervisor_connect_bind(sup);

    canopus_hook_engine_init();

    /* Wire legacy services v1 table */
    canopus_memset(&s_services, 0, sizeof(s_services));
    canopus_memset(&s_fs_obj, 0, sizeof(s_fs_obj));
    canopus_memset(&s_kv_obj, 0, sizeof(s_kv_obj));
    canopus_memset(&s_mem_obj, 0, sizeof(s_mem_obj));
    canopus_memset(&s_time_obj, 0, sizeof(s_time_obj));
    canopus_memset(&s_hook_obj, 0, sizeof(s_hook_obj));
    canopus_memset(&s_log_obj, 0, sizeof(s_log_obj));
    canopus_memset(&s_notify_obj, 0, sizeof(s_notify_obj));
    canopus_memset(&s_system_obj, 0, sizeof(s_system_obj));
    canopus_memset(&s_bt_obj, 0, sizeof(s_bt_obj));
    canopus_memset(&s_haptics_obj, 0, sizeof(s_haptics_obj));
    canopus_memset(&s_display_obj, 0, sizeof(s_display_obj));
    canopus_memset(&s_bus_obj, 0, sizeof(s_bus_obj));

    s_services.struct_size = sizeof(s_services);
    s_services.group_version[CANOPUS_SVC_MODULE] = 1u;
    s_services.group_version[CANOPUS_SVC_LOG] = 1u;
    s_services.group_version[CANOPUS_SVC_HOOK] = 1u;
    s_services.group_version[CANOPUS_SVC_SYSTEM] = 1u;
    s_services.group_version[CANOPUS_SVC_BLUETOOTH] = 1u;
    s_services.group_version[CANOPUS_SVC_KV] = 1u;
    s_services.group_version[CANOPUS_SVC_HAPTICS] = 1u;
    s_services.group_version[CANOPUS_SVC_DISPLAY] = 1u;
    s_services.group_version[CANOPUS_SVC_BUS] = 2u;
    s_services.group_version[CANOPUS_SVC_CONNECT] = s_connect_obj != 0 ? 1u : 0u;
    s_services.group_version[CANOPUS_SVC_LAUNCHER] =
        (sup->platform != 0 && sup->platform->launcher_publish != 0) ? 1u : 0u;

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

        s_fs_obj.open = svc_fs_open_flags;
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
        s_fs_obj.read_all = svc_fs_read_all;
        s_fs_obj.write_all = svc_fs_write_all;
        s_fs_obj.get_data_path = svc_fs_get_data_path;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->monotonic_ms != 0) {
        s_services.group_version[CANOPUS_SVC_TIME] = 1u;
        s_services.time_monotonic_ms = svc_time_monotonic_ms;
        s_time_obj.monotonic_ms = svc_time_monotonic_ms;
        s_time_obj.realtime_ms = svc_time_realtime_ms;
        s_time_obj.sleep_ms = svc_time_sleep_ms;
        s_time_obj.timer_create = svc_time_timer_create;
        s_time_obj.timer_start = svc_time_timer_start;
        s_time_obj.timer_stop = svc_time_timer_stop;
        s_time_obj.timer_destroy = svc_time_timer_destroy;
        s_time_obj.timer_set_background = svc_time_timer_set_background;
        if (sup->platform->start_background_pump != 0) s_services.group_version[CANOPUS_SVC_TIME] = 2u;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->heap_alloc != 0 &&
        sup->platform->heap_release != 0) {
        s_services.group_version[CANOPUS_SVC_MEM] = 1u;
        s_services.mem_alloc = svc_mem_alloc;
        s_services.mem_free = svc_mem_free;
        s_mem_obj.alloc = svc_mem_alloc;
        s_mem_obj.calloc = svc_mem_calloc;
        s_mem_obj.realloc = svc_mem_realloc;
        s_mem_obj.free = svc_mem_free;
        s_mem_obj.available = svc_mem_available;
        s_mem_obj.used = svc_mem_used;
    }

    if (sup != 0 && sup->platform != 0 && sup->platform->notify != 0) {
        s_services.group_version[CANOPUS_SVC_NOTIFY] = 1u;
        s_services.notify_post = svc_notify_post;
        s_notify_obj.post = svc_notify_post_obj;
        s_notify_obj.post_with_icon = svc_notify_post_with_icon;
    }

    /* KV Object */
    s_kv_obj.get_string = svc_kv_get_string;
    s_kv_obj.set_string = svc_kv_set_string;
    s_kv_obj.get_int = svc_kv_get_int;
    s_kv_obj.set_int = svc_kv_set_int;
    s_kv_obj.get_bool = svc_kv_get_bool;
    s_kv_obj.set_bool = svc_kv_set_bool;
    s_kv_obj.remove = svc_kv_remove;
    s_kv_obj.clear = svc_kv_clear;

    /* Hook Object */
    s_hook_obj.install = svc_hook_install;
    s_hook_obj.uninstall = svc_hook_uninstall;
    s_hook_obj.is_hooked = svc_hook_is_hooked;
    s_hook_obj.register_slot_map = svc_hook_register_slot_map;

    /* Log Object */
    s_log_obj.write = svc_log_write;
    s_log_obj.info = svc_log_info;
    s_log_obj.warn = svc_log_warn;
    s_log_obj.error = svc_log_error;
    s_log_obj.debug = svc_log_debug;

    /* System Object */
    s_system_obj.target_id = svc_system_target_id;
    s_system_obj.module_id = svc_system_module_id;
    s_system_obj.device_model = svc_system_device_model;
    s_system_obj.firmware_version = svc_system_firmware_version;
    s_system_obj.framework_revision = svc_system_framework_revision;
    s_system_obj.is_safe_mode = svc_system_is_safe_mode;
    s_system_obj.uptime_ms = svc_system_uptime_ms;
    s_system_obj.battery_level = svc_system_battery_level;
    s_system_obj.is_charging = svc_system_is_charging;
    s_system_obj.reboot = svc_system_reboot;
    s_system_obj.get_property = svc_system_get_property;

    /* Bluetooth Object */
    s_bt_obj.l2_send = svc_bt_l2_send;
    s_bt_obj.l2_register_rx = svc_bt_l2_register_rx;
    s_bt_obj.is_connected = svc_bt_is_connected;
    s_bt_obj.adapter_state = svc_bt_adapter_state;

    /* Haptics Object */
    s_haptics_obj.vibrate = svc_haptics_vibrate;
    s_haptics_obj.vibrate_pattern = svc_haptics_vibrate_pattern;
    s_haptics_obj.vibrate_preset = svc_haptics_vibrate_preset;
    s_haptics_obj.cancel = svc_haptics_cancel;

    /* Display Object */
    s_display_obj.get_brightness = svc_display_get_brightness;
    s_display_obj.set_brightness = svc_display_set_brightness;
    s_display_obj.is_screen_on = svc_display_is_screen_on;
    s_display_obj.wake_screen = svc_display_wake_screen;
    s_display_obj.acquire_wake_lock = svc_display_acquire_wake_lock;
    s_display_obj.release_wake_lock = svc_display_release_wake_lock;
    s_display_obj.get_resolution = svc_display_get_resolution;

    /* Bus Object */
    s_bus_obj.publish = svc_bus_publish;
    s_bus_obj.subscribe = svc_bus_subscribe;
    s_bus_obj.unsubscribe = svc_bus_unsubscribe;
    s_bus_obj.query_module = svc_bus_query_module;

    /* UI Object */
    s_ui_obj.capabilities = canopus_ui_context_capabilities;
    s_ui_obj.tree_begin = canopus_ui_tree_begin;
    s_ui_obj.tree_commit = canopus_ui_tree_commit;
    s_ui_obj.tree_abort = canopus_ui_tree_abort;
    s_ui_obj.end = canopus_ui_end;
    s_ui_obj.navigation_page = canopus_ui_navigation_page;
    s_ui_obj.section = canopus_ui_section;
    s_ui_obj.text = canopus_ui_text;
    s_ui_obj.status_row = canopus_ui_status_row;
    s_ui_obj.button = canopus_ui_button;
    s_ui_obj.action_row = canopus_ui_action_row;
    s_ui_obj.switch_row = canopus_ui_switch_row;
    s_ui_obj.navigation_header = canopus_ui_navigation_header;
    s_ui_obj.component = canopus_ui_component;
    s_ui_obj.node_set_style = canopus_ui_node_set_style;
    s_ui_obj.node_set_layout = canopus_ui_node_set_layout;
    s_ui_obj.set_entry = svc_ui_set_entry;
    s_ui_obj.close_page = svc_ui_close_page;
    s_ui_obj.node_set_visual = canopus_ui_node_set_visual;
    s_ui_obj.control = canopus_ui_control;
    s_ui_obj.post = canopus_supervisor_ui_post;
    s_launcher_obj.set_entry = svc_launcher_set_entry;

    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        s_contexts[i].struct_size = sizeof(struct canopus_context_v1);
        s_contexts[i].abi = (CANOPUS_ABI_MAJOR << 16) | CANOPUS_ABI_MINOR;
        s_contexts[i].services = &s_services;
        s_contexts[i].slot = i;
        s_contexts[i].target_id = (sup != 0 && sup->platform != 0) ? sup->platform->target_id : "";
        s_contexts[i].module_id = (const char *)sup->modules[i].module_id;

        s_contexts[i].fs = (s_services.group_version[CANOPUS_SVC_FS] != 0u) ? &s_fs_obj : 0;
        s_contexts[i].kv = &s_kv_obj;
        s_contexts[i].mem = (s_services.group_version[CANOPUS_SVC_MEM] != 0u) ? &s_mem_obj : 0;
        s_contexts[i].time = (s_services.group_version[CANOPUS_SVC_TIME] != 0u) ? &s_time_obj : 0;
        s_contexts[i].hook = &s_hook_obj;
        s_contexts[i].log = &s_log_obj;
        s_contexts[i].notify = (s_services.group_version[CANOPUS_SVC_NOTIFY] != 0u) ? &s_notify_obj : 0;
        s_contexts[i].system = &s_system_obj;
        s_contexts[i].bt = &s_bt_obj;
        s_contexts[i].haptics = &s_haptics_obj;
        s_contexts[i].display = &s_display_obj;
        s_contexts[i].bus = &s_bus_obj;
        s_contexts[i].ui = &s_ui_obj;
        s_contexts[i].connect = s_connect_obj;
        s_contexts[i].launcher = (s_services.group_version[CANOPUS_SVC_LAUNCHER] != 0u) ? &s_launcher_obj : 0;
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

    s_ui_jobs[slot].callback = 0;
    s_ui_jobs[slot].user_data = 0;
    s_ui_jobs_ready &= ~(1u << slot);

    /* Release all active hooks owned by this module */
    canopus_hook_release_slot(slot);

    /* Release any active timers owned by this module */
    for (i = 0u; i < CANOPUS_MAX_TIMERS; i++) {
        if (s_timers[i].used && s_timers[i].slot == slot) {
            canopus_memset(&s_timers[i], 0, sizeof(s_timers[i]));
        }
    }

    /* Release any bus subscriptions owned by this module */
    for (i = 0u; i < CANOPUS_BUS_MAX_SUBS; i++) {
        if (s_bus_subs[i].used && s_bus_subs[i].slot == slot) {
            canopus_memset(&s_bus_subs[i], 0, sizeof(s_bus_subs[i]));
        }
    }
    s_events.wanted = svc_events_wanted();

    /* Its UI page entry goes first, so the page being closed does not call a
     * module that is no longer there. */
    if (s_ui_entries[slot].used) {
        canopus_memset(&s_ui_entries[slot], 0, sizeof(s_ui_entries[slot]));
        if (sup->platform != 0 && sup->platform->ui_page_close != 0) {
            (void)sup->platform->ui_page_close(sup->platform_cookie, slot);
        }
    }

    /* Hardware the module still holds: vibration, wake locks, brightness,
     * L2 receive registrations and its CanopusConnect receiver. */
    canopus_supervisor_connect_release(slot);
    if (s_haptic.active && s_haptic.slot == slot) svc_haptic_stop();
    svc_wakelocks_release(slot);
    svc_brightness_release(slot);
    for (i = 0u; i < sizeof(s_l2_rx) / sizeof(s_l2_rx[0]); i++) {
        if (s_l2_rx[i].cb != 0 && s_l2_rx[i].slot == slot) {
            canopus_memset(&s_l2_rx[i], 0, sizeof(s_l2_rx[i]));
        }
    }

    /* Release open files */
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

    /* Release allocated memory blocks */
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
