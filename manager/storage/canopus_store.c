/*
 * canopus_store.c — transactional package store.
 *
 * Uses POSIX file operations; valid on both the host (for tests) and the
 * NuttX device filesystem. State writes are temp + fsync + atomic rename.
 */
#include "canopus_store.h"
#include "canopus_memory.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *SLOT_NAMES[CANOPUS_STORE_SLOTS] = {
    "active", "previous", "staged", "quarantined",
};

int canopus_store_init(struct canopus_store_v1 *store, const char *root)
{
    int n;
    if (store == 0 || root == 0 || root[0] == '\0' || root[0] != '/') {
        return -1; /* root must be canonical absolute and target-configured */
    }
    canopus_memset(store, 0, sizeof(*store));
    n = snprintf(store->root, sizeof(store->root), "%s", root);
    if (n < 0 || (size_t)n >= sizeof(store->root)) {
        store->root[0] = '\0';
        return -1; /* never a silent truncation of the configured root */
    }
    store->last_error = 0;
    return 0;
}

static int valid_package_id(const char *id)
{
    size_t i;
    if (id == 0 || id[0] == '\0') {
        return 0;
    }
    for (i = 0; id[i] != '\0'; i++) {
        char c = id[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '.' || c == '-' || c == '_')) {
            return 0;
        }
    }
    return 1;
}

int canopus_store_slot_path(struct canopus_store_v1 *store,
                            const char *package_id, int slot,
                            char *out, size_t out_size)
{
    int n;
    if (!valid_package_id(package_id)) {
        store->last_error = "invalid package id";
        return -1;
    }
    if (slot < 0 || slot >= CANOPUS_STORE_SLOTS) {
        store->last_error = "invalid slot";
        return -1;
    }
    n = snprintf(out, out_size, "%s/packages/%s/%s",
                 store->root, package_id, SLOT_NAMES[slot]);
    if (n < 0 || (size_t)n >= out_size) {
        store->last_error = "path too long";
        return -1;
    }
    return 0;
}

/* fsync the parent directory of `path` so a rename/unlink inside it is
 * durable across a crash. */
static void fsync_parent(const char *path)
{
    const char *slash = strrchr(path, '/');
    char dir[CANOPUS_STORE_PATH_MAX];
    int dfd;
    size_t dlen;
    if (slash == 0) {
        return;
    }
    dlen = (size_t)(slash - path);
    if (dlen == 0 || dlen >= sizeof(dir)) {
        return;
    }
    canopus_memcpy(dir, path, dlen);
    dir[dlen] = '\0';
    dfd = open(dir, O_RDONLY);
    if (dfd >= 0) {
        (void)fsync(dfd);
        (void)close(dfd);
    }
}

/* CAN-P1-014: writes loop over partial writes and EINTR, use an exclusive
 * temp name so concurrent writers never truncate each other, and fsync the
 * parent directory so the rename is durable. */
int canopus_store_write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[CANOPUS_STORE_PATH_MAX + 32];
    int fd = -1;
    int ok = -1;
    size_t plen, written;
    static uint32_t s_tmp_seq;

    if (path == 0 || (data == 0 && len > 0)) {
        return -1;
    }
    if (len > (size_t)SSIZE_MAX) {
        return -1; /* a single record can never exceed SSIZE_MAX */
    }
    plen = canopus_strlen(path);
    if (plen == 0 || plen + 32 >= sizeof(tmp)) {
        return -1;
    }
    s_tmp_seq++;
    if (snprintf(tmp, sizeof(tmp), "%s.tmp.%08x", path, (unsigned)s_tmp_seq) < 0) {
        return -1;
    }
    fd = open(tmp, O_WRONLY | O_CREAT | O_EXCL, 0640);
    if (fd < 0) {
        return -1;
    }
    written = 0;
    while (written < len) {
        ssize_t w = write(fd, (const char *)data + written, len - written);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto out;
        }
        if (w == 0) {
            goto out; /* no progress: fail rather than spin */
        }
        written += (size_t)w;
    }
    if (fsync(fd) != 0) {
        goto out;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto out;
    }
    fd = -1;
    if (rename(tmp, path) != 0) {
        goto out;
    }
    fsync_parent(path);
    ok = 0;
out:
    if (fd >= 0) {
        (void)close(fd);
    }
    if (ok != 0) {
        (void)unlink(tmp);
    }
    return ok;
}

static int dir_exists(const char *path)
{
    struct stat st;
    return path != 0 && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int canopus_store_has_staged(struct canopus_store_v1 *store,
                             const char *package_id)
{
    char p[CANOPUS_STORE_PATH_MAX];
    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                p, sizeof(p)) != 0) {
        return 0;
    }
    return dir_exists(p);
}

/* Moves directory `from` to `to`. Fails if `to` exists. */
static int move_dir(const char *from, const char *to)
{
    if (!dir_exists(from)) {
        return -1;
    }
    if (dir_exists(to)) {
        return -1;
    }
    return rename(from, to) == 0 ? 0 : -1;
}

static int mkdirs(const char *path)
{
    char tmp[CANOPUS_STORE_PATH_MAX];
    size_t i, n = canopus_strlen(path);
    struct stat st;
    if (path == 0 || n == 0 || n + 1 >= sizeof(tmp)) {
        return -1;
    }
    canopus_memcpy(tmp, path, n + 1);
    for (i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            if (mkdir(tmp, 0750) != 0) {
                if (errno != EEXIST || stat(tmp, &st) != 0 ||
                    !S_ISDIR(st.st_mode)) {
                    return -1; /* only EEXIST + a directory is success */
                }
            }
            tmp[i] = '/';
        }
    }
    if (mkdir(path, 0750) != 0 && errno != EEXIST) {
        if (stat(path, &st) != 0 || !S_ISDIR(st.st_mode)) {
            return -1;
        }
    }
    return 0;
}

/* ---- CAN-P1-006: transaction journal ------------------------------- */

static int txn_path(const struct canopus_store_v1 *store,
                    const char *package_id, char *out, size_t out_size)
{
    int n = snprintf(out, out_size, "%s/packages/%s/txn.state",
                     store->root, package_id);
    return (n < 0 || (size_t)n >= out_size) ? -1 : 0;
}

static int txn_write(const struct canopus_store_v1 *store,
                     const char *package_id, uint32_t state)
{
    char p[CANOPUS_STORE_PATH_MAX];
    uint8_t buf[4];
    if (txn_path(store, package_id, p, sizeof(p)) != 0) {
        return -1;
    }
    buf[0] = (uint8_t)(state & 0xff);
    buf[1] = (uint8_t)((state >> 8) & 0xff);
    buf[2] = (uint8_t)((state >> 16) & 0xff);
    buf[3] = (uint8_t)((state >> 24) & 0xff);
    return canopus_store_write_atomic(p, buf, sizeof(buf));
}

uint32_t canopus_store_txn_state(const struct canopus_store_v1 *store,
                                 const char *package_id)
{
    char p[CANOPUS_STORE_PATH_MAX];
    uint8_t buf[4];
    int fd;
    ssize_t n;
    if (txn_path(store, package_id, p, sizeof(p)) != 0) {
        return CANOPUS_STORE_TXN_NONE;
    }
    fd = open(p, O_RDONLY);
    if (fd < 0) {
        return CANOPUS_STORE_TXN_NONE;
    }
    n = read(fd, buf, sizeof(buf));
    (void)close(fd);
    if (n != 4) {
        return CANOPUS_STORE_TXN_NONE;
    }
    return (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) |
           ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
}

/* Recursive, controlled delete of a package slot directory. */
static int rmtree(const char *path)
{
    char child[CANOPUS_STORE_PATH_MAX];
    struct stat st;
    DIR *d;
    struct dirent *e;
    if (!dir_exists(path)) {
        return 0; /* idempotent */
    }
    d = opendir(path);
    if (d == 0) {
        return -1;
    }
    while ((e = readdir(d)) != 0) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0) {
            continue;
        }
        if (snprintf(child, sizeof(child), "%s/%s", path, e->d_name) < 0) {
            (void)closedir(d);
            return -1;
        }
        if (lstat(child, &st) != 0) {
            (void)closedir(d);
            return -1;
        }
        if (S_ISDIR(st.st_mode)) {
            if (rmtree(child) != 0) {
                (void)closedir(d);
                return -1;
            }
        } else {
            if (unlink(child) != 0) {
                (void)closedir(d);
                return -1;
            }
        }
    }
    (void)closedir(d);
    if (rmdir(path) != 0) {
        return -1;
    }
    fsync_parent(path);
    return 0;
}

int canopus_store_install_staged(struct canopus_store_v1 *store,
                                 const char *package_id)
{
    char staged[CANOPUS_STORE_PATH_MAX], active[CANOPUS_STORE_PATH_MAX];
    char previous[CANOPUS_STORE_PATH_MAX];

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                staged, sizeof(staged)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_PREVIOUS,
                                previous, sizeof(previous)) != 0) {
        return -1;
    }
    if (!dir_exists(staged)) {
        store->last_error = "no staged payload";
        return -1;
    }
    /* CAN-P1-006: journaled transaction, idempotently recoverable. */
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_PREPARED) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    fsync_parent(active);
    /* a stale previous never blocks the update: clear it first */
    if (dir_exists(previous)) {
        if (rmtree(previous) != 0) {
            store->last_error = "previous slot cleanup failed";
            return -1;
        }
    }
    if (txn_write(store, package_id,
                  CANOPUS_STORE_TXN_ACTIVE_TO_PREVIOUS) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    if (dir_exists(active)) {
        if (move_dir(active, previous) != 0) {
            store->last_error = "active->previous rename failed";
            return -1;
        }
    }
    fsync_parent(active);
    if (txn_write(store, package_id,
                  CANOPUS_STORE_TXN_STAGED_TO_ACTIVE) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    if (move_dir(staged, active) != 0) {
        store->last_error = "staged->active rename failed";
        return -1;
    }
    fsync_parent(active);
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_COMMITTED) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_NONE) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    fsync_parent(active);
    return 0;
}

int canopus_store_rollback(struct canopus_store_v1 *store,
                           const char *package_id)
{
    char active[CANOPUS_STORE_PATH_MAX], previous[CANOPUS_STORE_PATH_MAX];
    char junk[CANOPUS_STORE_PATH_MAX + 16];
    static uint32_t s_old_seq;

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_PREVIOUS,
                                previous, sizeof(previous)) != 0) {
        return -1;
    }
    if (!dir_exists(previous)) {
        store->last_error = "no previous payload";
        return -1;
    }
    /* CAN-P1-006: journaled rollback. The discarded active goes to a
     * transaction-tagged `.old.<seq>` so boot recovery can identify it. */
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_PREPARED) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    if (dir_exists(active)) {
        s_old_seq++;
        if (snprintf(junk, sizeof(junk), "%s.old.%08x", active,
                     (unsigned)s_old_seq) < 0 ||
            canopus_strlen(junk) >= sizeof(junk)) {
            store->last_error = "rollback path too long";
            return -1; /* CAN-P1-014: check .old truncation */
        }
        if (move_dir(active, junk) != 0) {
            store->last_error = "active busy";
            return -1;
        }
        fsync_parent(active);
    }
    if (txn_write(store, package_id,
                  CANOPUS_STORE_TXN_STAGED_TO_ACTIVE) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    if (move_dir(previous, active) != 0) {
        store->last_error = "previous->active failed";
        return -1;
    }
    fsync_parent(active);
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_NONE) != 0) {
        store->last_error = "journal write failed";
        return -1;
    }
    fsync_parent(active);
    return 0;
}

int canopus_store_quarantine(struct canopus_store_v1 *store,
                             const char *package_id)
{
    char active[CANOPUS_STORE_PATH_MAX], quar[CANOPUS_STORE_PATH_MAX];

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_QUARANTINED,
                                quar, sizeof(quar)) != 0) {
        return -1;
    }
    if (move_dir(active, quar) != 0) {
        store->last_error = "quarantine move failed";
        return -1;
    }
    return 0;
}

int canopus_store_remove_slot(struct canopus_store_v1 *store,
                              const char *package_id, int slot)
{
    char p[CANOPUS_STORE_PATH_MAX];
    if (canopus_store_slot_path(store, package_id, slot, p, sizeof(p)) != 0) {
        return -1;
    }
    /* CAN-P1-006: recursive delete — a non-empty slot (active payload) is
     * fully removed, not rmdir'd. */
    return rmtree(p);
}

/* Boot-time recovery: read the journal and complete or undo an interrupted
 * transaction idempotently. Exactly one valid active slot remains. */
int canopus_store_recover(struct canopus_store_v1 *store,
                          const char *package_id)
{
    char staged[CANOPUS_STORE_PATH_MAX], active[CANOPUS_STORE_PATH_MAX];
    char previous[CANOPUS_STORE_PATH_MAX];
    uint32_t state;

    if (store == 0 || package_id == 0) {
        return -1;
    }
    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                staged, sizeof(staged)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_PREVIOUS,
                                previous, sizeof(previous)) != 0) {
        return -1;
    }
    state = canopus_store_txn_state(store, package_id);
    switch (state) {
    case CANOPUS_STORE_TXN_NONE:
        return 0;
    case CANOPUS_STORE_TXN_PREPARED:
        /* nothing durable was moved: drop the staged payload, keep active */
        (void)rmtree(staged);
        break;
    case CANOPUS_STORE_TXN_ACTIVE_TO_PREVIOUS:
        /* active was moved aside but staged never promoted: restore it */
        if (dir_exists(previous) && !dir_exists(active)) {
            (void)move_dir(previous, active);
        }
        (void)rmtree(staged);
        break;
    case CANOPUS_STORE_TXN_STAGED_TO_ACTIVE:
    case CANOPUS_STORE_TXN_COMMITTED:
        /* the promotion landed: commit it (previous retained for rollback) */
        (void)rmtree(staged);
        break;
    case CANOPUS_STORE_TXN_CLEANUP:
    default:
        /* unknown/leftover journal: clear it, leave active untouched */
        break;
    }
    /* idempotent completion is durable only after the journal clears */
    if (txn_write(store, package_id, CANOPUS_STORE_TXN_NONE) != 0) {
        store->last_error = "journal clear failed";
        return -1;
    }
    fsync_parent(active);
    return 0;
}

/* Convenience for tests: ensure the packages/<id> tree exists. */
int canopus_store_ensure_package_dir(struct canopus_store_v1 *store,
                                     const char *package_id)
{
    char p[CANOPUS_STORE_PATH_MAX];
    size_t n;
    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                p, sizeof(p)) != 0) {
        return -1;
    }
    /* p ends with "/staged"; the parent is the package dir. Guard the slice
     * so a short path can never underflow. */
    n = canopus_strlen(p);
    if (n < 7u) {
        return -1;
    }
    p[n - 7u] = '\0';
    return mkdirs(p);
}
