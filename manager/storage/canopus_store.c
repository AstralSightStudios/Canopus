/*
 * canopus_store.c — transactional package store.
 *
 * Uses POSIX file operations; valid on both the host (for tests) and the
 * NuttX device filesystem. State writes are temp + fsync + atomic rename.
 */
#include "canopus_store.h"
#include "canopus_memory.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *SLOT_NAMES[CANOPUS_STORE_SLOTS] = {
    "active", "previous", "staged", "quarantined",
};

void canopus_store_init(struct canopus_store_v1 *store, const char *root)
{
    canopus_memset(store, 0, sizeof(*store));
    snprintf(store->root, sizeof(store->root), "%s", root);
    store->last_error = 0;
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

int canopus_store_slot_path(const struct canopus_store_v1 *store,
                            const char *package_id, int slot,
                            char *out, size_t out_size)
{
    int n;
    if (!valid_package_id(package_id)) {
        ((struct canopus_store_v1 *)store)->last_error = "invalid package id";
        return -1;
    }
    if (slot < 0 || slot >= CANOPUS_STORE_SLOTS) {
        ((struct canopus_store_v1 *)store)->last_error = "invalid slot";
        return -1;
    }
    n = snprintf(out, out_size, "%s/packages/%s/%s",
                 store->root, package_id, SLOT_NAMES[slot]);
    if (n < 0 || (size_t)n >= out_size) {
        ((struct canopus_store_v1 *)store)->last_error = "path too long";
        return -1;
    }
    return 0;
}

int canopus_store_write_atomic(const char *path, const void *data, size_t len)
{
    char tmp[200];
    int fd = -1;
    int ok = -1;
    size_t plen = canopus_strlen(path);

    if (plen + 5 >= sizeof(tmp)) {
        return -1;
    }
    canopus_memcpy(tmp, path, plen);
    canopus_memcpy(tmp + plen, ".tmp", 5);

    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) {
        return -1;
    }
    if (write(fd, data, len) != (ssize_t)len) {
        goto out;
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
    ok = 0;
out:
    if (fd >= 0) {
        close(fd);
    }
    if (ok != 0) {
        unlink(tmp);
    }
    return ok;
}

static int dir_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int canopus_store_has_staged(const struct canopus_store_v1 *store,
                             const char *package_id)
{
    char p[200];
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
    char tmp[200];
    size_t i, n = canopus_strlen(path);
    if (n + 1 >= sizeof(tmp)) {
        return -1;
    }
    canopus_memcpy(tmp, path, n + 1);
    for (i = 1; i < n; i++) {
        if (tmp[i] == '/') {
            tmp[i] = '\0';
            mkdir(tmp, 0750);
            tmp[i] = '/';
        }
    }
    return mkdir(path, 0750) == 0 ? 0 : -1;
}

int canopus_store_install_staged(const struct canopus_store_v1 *store,
                                 const char *package_id)
{
    char staged[200], active[200], previous[200];
    struct canopus_store_v1 *self = (struct canopus_store_v1 *)store;

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                staged, sizeof(staged)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_PREVIOUS,
                                previous, sizeof(previous)) != 0) {
        return -1;
    }
    if (!dir_exists(staged)) {
        self->last_error = "no staged payload";
        return -1;
    }
    /* preserve current active as previous (never replaced in place) */
    if (dir_exists(active)) {
        if (move_dir(active, previous) != 0) {
            self->last_error = "previous slot busy";
            return -1;
        }
    }
    if (move_dir(staged, active) != 0) {
        self->last_error = "staged->active rename failed";
        return -1;
    }
    return 0;
}

int canopus_store_rollback(const struct canopus_store_v1 *store,
                           const char *package_id)
{
    char active[200], previous[200], junk[200];
    struct canopus_store_v1 *self = (struct canopus_store_v1 *)store;

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_PREVIOUS,
                                previous, sizeof(previous)) != 0) {
        return -1;
    }
    if (!dir_exists(previous)) {
        self->last_error = "no previous payload";
        return -1;
    }
    /* discard the failed active, then promote previous */
    if (dir_exists(active)) {
        snprintf(junk, sizeof(junk), "%s.old", active);
        if (move_dir(active, junk) != 0) {
            self->last_error = "active busy";
            return -1;
        }
    }
    if (move_dir(previous, active) != 0) {
        self->last_error = "previous->active failed";
        return -1;
    }
    return 0;
}

int canopus_store_quarantine(const struct canopus_store_v1 *store,
                             const char *package_id)
{
    char active[200], quar[200];
    struct canopus_store_v1 *self = (struct canopus_store_v1 *)store;

    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_ACTIVE,
                                active, sizeof(active)) != 0 ||
        canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_QUARANTINED,
                                quar, sizeof(quar)) != 0) {
        return -1;
    }
    if (move_dir(active, quar) != 0) {
        self->last_error = "quarantine move failed";
        return -1;
    }
    return 0;
}

int canopus_store_remove_slot(const struct canopus_store_v1 *store,
                              const char *package_id, int slot)
{
    char p[200];
    if (canopus_store_slot_path(store, package_id, slot, p, sizeof(p)) != 0) {
        return -1;
    }
    if (!dir_exists(p)) {
        return 0; /* idempotent */
    }
    return rmdir(p) == 0 ? 0 : -1;
}

/* Convenience for tests: ensure the packages/<id> tree exists. */
int canopus_store_ensure_package_dir(const struct canopus_store_v1 *store,
                                     const char *package_id)
{
    char p[200];
    if (canopus_store_slot_path(store, package_id, CANOPUS_STORE_SLOT_STAGED,
                                p, sizeof(p)) != 0) {
        return -1;
    }
    /* p ends with /staged; parent is the package dir */
    p[canopus_strlen(p) - 7] = '\0';
    return mkdirs(p);
}
