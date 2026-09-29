/* Host-testable dual-slot Supervisor registry store for P65. */
#ifndef CANOPUS_MANAGER_P65_REGISTRY_H
#define CANOPUS_MANAGER_P65_REGISTRY_H

#include <stdint.h>
#include "canopus_supervisor.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_P65_REGISTRY_SLOT_COUNT 2u
#define CANOPUS_P65_REGISTRY_ENVELOPE_SIZE 20u
#define CANOPUS_P65_REGISTRY_RECORD_SIZE \
    (CANOPUS_P65_REGISTRY_ENVELOPE_SIZE + CANOPUS_SUP_REGISTRY_SIZE)
#define CANOPUS_P65_REGISTRY_MAGIC 0x52353650u /* "P65R" */
#define CANOPUS_P65_REGISTRY_VERSION 1u

enum canopus_p65_registry_error_v1 {
    CANOPUS_P65_REGISTRY_OK = 0,
    CANOPUS_P65_REGISTRY_ABSENT = 1,
    CANOPUS_P65_REGISTRY_ERR_ARGUMENT = -1,
    CANOPUS_P65_REGISTRY_ERR_IO = -2,
    CANOPUS_P65_REGISTRY_ERR_FORMAT = -3,
    CANOPUS_P65_REGISTRY_ERR_SEQUENCE = -4,
    CANOPUS_P65_REGISTRY_ERR_WRITE = -5,
    CANOPUS_P65_REGISTRY_ERR_VERIFY = -6,
};

/* read_slot returns 0 for a complete read, 1 for a missing slot, or a
 * negative I/O error. On success, *actual_size is the exact on-disk size; it
 * may exceed capacity to report an oversized/corrupt slot, but the callback
 * must never write beyond capacity. write_slot replaces/truncates exactly one
 * slot and reports open/write/close errors. The store itself read-back
 * verifies every successful write. The provider owns slot paths and must not
 * mutate the other slot. Calls must be serialized. There is no fsync hook:
 * CRC/read-back and fallback are corruption defenses, not proven power-loss
 * durability. CRC is not authentication; artifact receipts remain separate. */
struct canopus_p65_registry_io_v1 {
    int (*read_slot)(void *cookie, uint32_t slot, uint8_t *buffer,
                     uint32_t capacity, uint32_t *actual_size);
    int (*write_slot)(void *cookie, uint32_t slot, const uint8_t *buffer,
                      uint32_t size);
};

/* Normalized file operations, not firmware function-pointer signatures.
 * open(replace=1) must create/truncate only the requested file; replace=0
 * opens read-only. Return a nonnegative handle, -2 for a missing read-only
 * file, or another negative error. size returns the exact byte length.
 * read/write return transferred bytes (short transfers are supported), or a
 * negative error. Providers handle interrupted syscalls themselves. close
 * returns zero on success. No firmware addresses or open flags are assumed.
 * Paths and ops must outlive the adapter, name distinct files, and be owned
 * by the caller. The directory must already exist. Access must be serialized;
 * external writers/path aliases are unsupported. No durability is promised. */
struct canopus_p65_registry_file_ops_v1 {
    int (*open)(void *cookie, const char *path, int replace);
    int (*size)(void *cookie, int handle, uint32_t *size);
    int32_t (*read)(void *cookie, int handle, uint8_t *buffer, uint32_t size);
    int32_t (*write)(void *cookie, int handle, const uint8_t *buffer,
                     uint32_t size);
    int (*close)(void *cookie, int handle);
};

struct canopus_manager_p65_registry_files_v1 {
    const struct canopus_p65_registry_file_ops_v1 *ops;
    void *cookie;
    const char *paths[CANOPUS_P65_REGISTRY_SLOT_COUNT];
};

int canopus_manager_p65_registry_files_init(
    struct canopus_manager_p65_registry_files_v1 *files,
    const struct canopus_p65_registry_file_ops_v1 *ops, void *cookie,
    const char *slot0_path, const char *slot1_path);

extern const struct canopus_p65_registry_io_v1
    canopus_manager_p65_registry_file_io;

/* Caller-owned state; scratch is kept out of the Supervisor's call stack. */
struct canopus_manager_p65_registry_store_v1 {
    const struct canopus_p65_registry_io_v1 *io;
    void *io_cookie;
    uint32_t last_sequence;
    uint32_t last_slot;
    int32_t last_error;
    uint8_t scratch[CANOPUS_P65_REGISTRY_RECORD_SIZE];
};

int canopus_manager_p65_registry_store_init(
    struct canopus_manager_p65_registry_store_v1 *store,
    const struct canopus_p65_registry_io_v1 *io, void *io_cookie);

/* These hooks match canopus_sup_platform_v1.persist/restore. The envelope is
 * internal to P65; callers still pass/receive the generic CRD1 registry.
 * Caller data buffers must not overlap the store's internal scratch area. */
int canopus_manager_p65_registry_persist(void *cookie,
                                         const uint8_t *data, uint32_t len);
int canopus_manager_p65_registry_restore(void *cookie,
                                         uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_MANAGER_P65_REGISTRY_H */
