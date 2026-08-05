/* canopus_client_posix.c — libc/NuttX fd syscall adapter. */
#include "canopus_client.h"
#include <fcntl.h>
#include <unistd.h>

static int32_t posix_open(void *cookie, const char *path)
{
    (void)cookie;
    return (int32_t)open(path, O_RDWR);
}

static int32_t posix_close(void *cookie, int32_t fd)
{
    (void)cookie;
    return (int32_t)close((int)fd);
}

static int32_t posix_read(void *cookie, int32_t fd, void *buffer,
                          uint32_t count)
{
    ssize_t result;
    (void)cookie;
    result = read((int)fd, buffer, (size_t)count);
    if (result < 0 || (uint64_t)result > 0x7fffffffu) {
        return -1;
    }
    return (int32_t)result;
}

static int32_t posix_write(void *cookie, int32_t fd, const void *buffer,
                           uint32_t count)
{
    ssize_t result;
    (void)cookie;
    result = write((int)fd, buffer, (size_t)count);
    if (result < 0 || (uint64_t)result > 0x7fffffffu) {
        return -1;
    }
    return (int32_t)result;
}

static const struct canopus_client_io_v1 posix_io = {
    sizeof(struct canopus_client_io_v1),
    CANOPUS_CLIENT_ABI_MAJOR,
    CANOPUS_CLIENT_ABI_MINOR,
    posix_open,
    posix_close,
    posix_read,
    posix_write,
};

const struct canopus_client_io_v1 *canopus_client_posix_io(void)
{
    return &posix_io;
}
