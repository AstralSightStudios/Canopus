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

#define CANOPUS_STORE_PATH_MAX 200u

/* CAN-P1-006: transaction journal states. Every slot mutation records the
 * step it is about to perform (PREPARED before moving anything, then per-
 * rename states) and clears it once durable. Boot-time recovery reads the
 * journal and completes/undoes idempotently so exactly one valid active
 * slot remains. */
enum canopus_store_txn_state {
    CANOPUS_STORE_TXN_NONE = 0,
    CANOPUS_STORE_TXN_PREPARED,
    CANOPUS_STORE_TXN_ACTIVE_TO_PREVIOUS,
    CANOPUS_STORE_TXN_STAGED_TO_ACTIVE,
    CANOPUS_STORE_TXN_COMMITTED,
    CANOPUS_STORE_TXN_CLEANUP,
};

struct canopus_store_v1 {
    char root[160];
    /* 0 when the last operation succeeded; else a static message. */
    const char *last_error;
};

/* Initializes the store root (does not touch the filesystem yet). Returns
 * 0 on success, -1 when the root is NULL/empty/relative or does not fit
 * (never a silent truncation). */
int canopus_store_init(struct canopus_store_v1 *store, const char *root);

/* Builds the path for a package slot into out (nul-terminated). Returns -1
 * if the package id is not a valid token. */
int canopus_store_slot_path(struct canopus_store_v1 *store,
                            const char *package_id, int slot,
                            char *out, size_t out_size);

/* Atomically writes `len` bytes to `path` (temp + fsync + rename). */
int canopus_store_write_atomic(const char *path, const void *data, size_t len);

/* Stage directory: returns 0 when the staged slot exists and is non-empty. */
int canopus_store_has_staged(struct canopus_store_v1 *store,
                             const char *package_id);

/* Installs the staged payload as active. Previous active is moved to the
 * previous slot first (never replaced in place). */
int canopus_store_install_staged(struct canopus_store_v1 *store,
                                 const char *package_id);

/* Rolls the previous slot back to active. */
int canopus_store_rollback(struct canopus_store_v1 *store,
                           const char *package_id);

/* Moves the active payload to quarantined. */
int canopus_store_quarantine(struct canopus_store_v1 *store,
                             const char *package_id);

/* Removes a slot directory recursively (idempotent; a non-empty slot is
 * fully deleted, not just rmdir'd). */
int canopus_store_remove_slot(struct canopus_store_v1 *store,
                              const char *package_id, int slot);

/* Test convenience: creates packages/<id>/ with the staged slot. */
int canopus_store_ensure_package_dir(struct canopus_store_v1 *store,
                                     const char *package_id);

/* Boot-time recovery: reads the package's transaction journal and completes
 * or undoes an interrupted install/rollback idempotently, leaving exactly
 * one valid active slot. Returns 0 on success (including nothing to do). */
int canopus_store_recover(struct canopus_store_v1 *store,
                          const char *package_id);

/* Test/diagnostics accessor: returns the current journal state for a
 * package (CANOPUS_STORE_TXN_NONE when absent or unreadable). */
uint32_t canopus_store_txn_state(const struct canopus_store_v1 *store,
                                 const char *package_id);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_STORE_H */
