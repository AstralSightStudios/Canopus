/* Experimental P65 fd provider for the host-tested dual-slot registry.
 * No guessed stat/fstat layout or fsync/rename durability assumption. */
#include "canopus_p65_abi.h"
#include "canopus_manager_p65_registry.h"

typedef int (*open_fn)(const char *, int, ...);
typedef int (*close_fn)(int);
typedef int32_t (*read_fn)(int, void *, uint32_t);
typedef int32_t (*write_fn)(int, const void *, uint32_t);
typedef int (*unlink_fn)(const char *);
typedef int *(*errno_fn)(void);
#ifndef FW
#define FW(address, type) ((type)(uintptr_t)(address))
#endif
static const char *const paths[2] = {
#ifdef CANOPUS_P65_RECOVERED_RUNTIME
    "/data/canopus/p65-runtime-registry0.bin", "/data/canopus/p65-runtime-registry1.bin",
#else
    "/data/canopus/registry0.bin", "/data/canopus/registry1.bin",
#endif
};
static struct canopus_manager_p65_registry_store_v1 store;
static uint8_t initialized;

static int error_number(void)
{
    int *value = FW(CANOPUS_SUP_NUTTX_ERRNO_LOCATION, errno_fn)();
    return value ? *value : 0;
}
static int read_slot(void *cookie, uint32_t slot, uint8_t *buffer,
                     uint32_t capacity, uint32_t *actual)
{
    uint32_t used = 0;
    uint8_t extra[64];
    int fd, rc = 0;
    (void)cookie;
    *actual = 0;
    if (slot >= 2 || capacity != CANOPUS_P65_REGISTRY_RECORD_SIZE) return -1;
    fd = FW(CANOPUS_SUP_NUTTX_OPEN, open_fn)(paths[slot], 1);
    if (fd < 0) return error_number() == 2 ? 1 : -1;
    while (used < capacity) {
        int32_t got = FW(CANOPUS_SUP_NUTTX_READ, read_fn)(fd, buffer + used,
                                                       capacity - used);
        if (got < 0 || (uint32_t)got > capacity - used) { rc = -1; break; }
        if (got == 0) break;
        used += (uint32_t)got;
    }
    if (rc == 0 && used == capacity) {
        /* Count excess bytes without writing beyond capacity, so the core
         * can classify a corrupt length and fall back to the other slot.
         * Bound work for a hostile/external file; access is serialized. */
        for (;;) {
            int32_t got = FW(CANOPUS_SUP_NUTTX_READ, read_fn)(fd, extra, sizeof(extra));
            if (got < 0 || (uint32_t)got > sizeof(extra)) { rc = -1; break; }
            if (got == 0) break;
            used += (uint32_t)got;
            if (used > 65536u) { rc = -1; break; }
        }
    }
    if (FW(CANOPUS_SUP_NUTTX_CLOSE, close_fn)(fd) != 0) rc = -1;
    if (rc == 0) *actual = used;
    return rc;
}
static int write_slot(void *cookie, uint32_t slot, const uint8_t *buffer,
                      uint32_t size)
{
    uint32_t used = 0;
    int fd, rc = 0;
    (void)cookie;
    if (slot >= 2 || size != CANOPUS_P65_REGISTRY_RECORD_SIZE) return -1;
    /* Delete only the inactive slot. If replacement fails, the previous
     * slot survives. Use recovered O_WRONLY|O_CREAT, not guessed O_TRUNC. */
    if (FW(CANOPUS_SUP_NUTTX_UNLINK, unlink_fn)(paths[slot]) != 0 &&
        error_number() != 2) return -1;
    fd = FW(CANOPUS_SUP_NUTTX_OPEN, open_fn)(paths[slot], 2 | 4, 0600);
    if (fd < 0) return -1;
    while (used < size) {
        int32_t count = FW(CANOPUS_SUP_NUTTX_WRITE, write_fn)(fd, buffer + used,
                                                           size - used);
        if (count <= 0 || (uint32_t)count > size - used) { rc = -1; break; }
        used += (uint32_t)count;
    }
    if (FW(CANOPUS_SUP_NUTTX_CLOSE, close_fn)(fd) != 0) rc = -1;
    return rc;
}
static const struct canopus_p65_registry_io_v1 io = { read_slot, write_slot };
static int init_store(void)
{
    if (initialized) return 0;
    if (canopus_manager_p65_registry_store_init(&store, &io, 0) != 0) return -1;
    initialized = 1;
    return 0;
}
int canopus_p65_registry_firmware_persist(const uint8_t *data, uint32_t size)
{
    if (init_store() != 0) return -1;
    return canopus_manager_p65_registry_persist(&store, data, size);
}
int canopus_p65_registry_firmware_restore(uint8_t *data, uint32_t size)
{
    if (init_store() != 0) return -1;
    return canopus_manager_p65_registry_restore(&store, data, size);
}
