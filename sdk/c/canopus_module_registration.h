#ifndef CANOPUS_MODULE_REGISTRATION_H
#define CANOPUS_MODULE_REGISTRATION_H

#include <stdint.h>

#define CANOPUS_MODULE_REGISTRATION_MAGIC 0x31524d43u /* "CMR1" */
#define CANOPUS_MODULE_REGISTRATION_SIZE 40u

struct canopus_module_registration_v1 {
    uint32_t magic;
    uint32_t descriptor;
    uint8_t module_id[32];
};

static inline int canopus_module_registration_is_frame(const void *buffer,
                                                        uint32_t count)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    return bytes != 0 && count == CANOPUS_MODULE_REGISTRATION_SIZE &&
           bytes[0] == (uint8_t)CANOPUS_MODULE_REGISTRATION_MAGIC &&
           bytes[1] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 8) &&
           bytes[2] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 16) &&
           bytes[3] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 24);
}

/* Exact fd-facing transport used by stock-modlib targets. A CMR1 write is
 * one atomic transaction: do not retry partial writes as a byte stream. The
 * Supervisor accepts it only while loading the matching module slot. */
struct canopus_module_registration_io_v1 {
    int (*open)(const char *path, int flags, ...);
    int32_t (*write)(int fd, const void *buffer, uint32_t size);
    int (*close)(int fd);
    int write_flags;
};

/* A successful write publishes descriptor pointers into the Supervisor.
 * Close failure cannot revoke that publication: return success and report
 * it separately, so modlib does not free a now-referenced module image.
 * Registered modules must retain their image until the Supervisor releases
 * all descriptor references (the P65 experimental path uses reboot). */
static inline int canopus_module_register_fd(
    const struct canopus_module_registration_io_v1 *io,
    uint32_t descriptor, const char *module_id, int *close_error)
{
    uint8_t frame[CANOPUS_MODULE_REGISTRATION_SIZE];
    volatile uint8_t *bytes = frame;
    uint32_t length = 0u, i;
    int fd, close_result;
    int32_t written;
    if (close_error != 0) *close_error = 0;
    if (io == 0 || io->open == 0 || io->write == 0 || io->close == 0 ||
        descriptor == 0u || module_id == 0) return -1;
    while (length < 32u && module_id[length] != '\0') length++;
    if (length == 0u || length == 32u) return -1;
    for (i = 0u; i < sizeof(frame); i++) bytes[i] = 0u;
    for (i = 0u; i < 4u; i++) {
        bytes[i] = (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> (8u * i));
        bytes[4u + i] = (uint8_t)(descriptor >> (8u * i));
    }
    for (i = 0u; i < length; i++) bytes[8u + i] = (uint8_t)module_id[i];
    fd = io->open("/dev/canopus", io->write_flags);
    if (fd < 0) return -1;
    written = io->write(fd, frame, sizeof(frame));
    close_result = io->close(fd);
    if (close_error != 0) *close_error = close_result;
    return written == (int32_t)sizeof(frame) ? 0 : -1;
}

#endif /* CANOPUS_MODULE_REGISTRATION_H */
