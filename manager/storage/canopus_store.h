/*
 * canopus_store.h — transactional package store (CAN-DEV-003).
 *
 * Slots per package: active / previous / staged / quarantined. All state
 * writes go through write-temp + fsync + atomic rename so a power loss
 * never corrupts the active manifest.
 */
#ifndef CANOPUS_STORE_H
#define CANOPUS_STORE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_STORE_SLOT_ACTIVE     0
#define CANOPUS_STORE_SLOT_PREVIOUS   1
#define CANOPUS_STORE_SLOT_STAGED     2
#define CANOPUS_STORE_SLOT_QUARANTINED 3
#define CANOPUS_STORE_SLOTS           4

struct canopus_store_v1 {
    char root[160];
    /* 0 when the last operation succeeded; else a static message. */
    const char *last_error;
};

/* Initializes the store root (does not touch the filesystem yet). */
void canopus_store_init(struct canopus_store_v1 *store, const char *root);

/* Builds the path for a package slot into out (nul-terminated). Returns -1
 * if the package id is not a valid token. */
int canopus_store_slot_path(const struct canopus_store_v1 *store,
                            const char *package_id, int slot,
                            char *out, size_t out_size);

/* Atomically writes `len` bytes to `path` (temp + fsync + rename). */
int canopus_store_write_atomic(const char *path, const void *data, size_t len);

/* Stage directory: returns 0 when the staged slot exists and is non-empty. */
int canopus_store_has_staged(const struct canopus_store_v1 *store,
                             const char *package_id);

/* Installs the staged payload as active. Previous active is moved to the
 * previous slot first (never replaced in place). */
int canopus_store_install_staged(const struct canopus_store_v1 *store,
                                 const char *package_id);

/* Rolls the previous slot back to active. */
int canopus_store_rollback(const struct canopus_store_v1 *store,
                           const char *package_id);

/* Moves the active payload to quarantined. */
int canopus_store_quarantine(const struct canopus_store_v1 *store,
                             const char *package_id);

/* Removes a slot directory (idempotent). */
int canopus_store_remove_slot(const struct canopus_store_v1 *store,
                              const char *package_id, int slot);

/* Test convenience: creates packages/<id>/ with the staged slot. */
int canopus_store_ensure_package_dir(const struct canopus_store_v1 *store,
                                     const char *package_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_STORE_H */
