/* P65 in-process client transport: same CPC2 bytes, no character device. */
#include "canopus_manager_p65_transport.h"
#include "canopus_supervisor.h"

#define CANOPUS_P65_LOCAL_FD 0x5045

static int p65_path_matches(const char *path)
{
    static const char expected[] = CANOPUS_CLIENT_DEVICE_PATH;
    uint32_t i;

    if (path == 0) {
        return 0;
    }
    for (i = 0u; i < sizeof(expected); i++) {
        if (path[i] != expected[i]) {
            return 0;
        }
        if (expected[i] == '\0') {
            return 1;
        }
    }
    return 0;
}

static int32_t p65_local_open(void *cookie, const char *path)
{
    if (cookie == 0 || !p65_path_matches(path)) {
        return -1;
    }
    return CANOPUS_P65_LOCAL_FD;
}

static int32_t p65_local_close(void *cookie, int32_t fd)
{
    return cookie != 0 && fd == CANOPUS_P65_LOCAL_FD ? 0 : -1;
}

static int32_t p65_local_read(void *cookie, int32_t fd, void *buffer,
                              uint32_t count)
{
    if (cookie == 0 || fd != CANOPUS_P65_LOCAL_FD) {
        return -1;
    }
    return canopus_supervisor_device_read(
        (struct canopus_supervisor_v1 *)cookie, buffer, count);
}

static int32_t p65_local_write(void *cookie, int32_t fd,
                               const void *buffer, uint32_t count)
{
    if (cookie == 0 || fd != CANOPUS_P65_LOCAL_FD) {
        return -1;
    }
    return canopus_supervisor_device_write(
        (struct canopus_supervisor_v1 *)cookie, buffer, count);
}

static const struct canopus_client_io_v1 p65_local_io = {
    sizeof(struct canopus_client_io_v1),
    CANOPUS_CLIENT_ABI_MAJOR,
    CANOPUS_CLIENT_ABI_MINOR,
    p65_local_open,
    p65_local_close,
    p65_local_read,
    p65_local_write,
};

const struct canopus_client_io_v1 *
canopus_manager_p65_local_client_io(void)
{
    return &p65_local_io;
}
