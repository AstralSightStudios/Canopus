/*
 * canopus_client.h — bounded CPC2 client transport for /dev/canopus.
 */
#ifndef CANOPUS_CLIENT_H
#define CANOPUS_CLIENT_H

#include <stdint.h>
#include "canopus_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_CLIENT_ABI_MAJOR 1u
#define CANOPUS_CLIENT_ABI_MINOR 0u
#define CANOPUS_CLIENT_DEVICE_PATH "/dev/canopus"
#define CANOPUS_CLIENT_WIRE_CAPACITY \
    (CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_PROTO_MAX_PAYLOAD)

#define CANOPUS_CLIENT_OK             0
#define CANOPUS_CLIENT_ERR_ARGUMENT  -1
#define CANOPUS_CLIENT_ERR_STATE     -2
#define CANOPUS_CLIENT_ERR_IO        -3
#define CANOPUS_CLIENT_ERR_PROTOCOL  -4

struct canopus_client_io_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    int32_t (*open)(void *cookie, const char *path);
    int32_t (*close)(void *cookie, int32_t fd);
    int32_t (*read)(void *cookie, int32_t fd, void *buffer, uint32_t count);
    int32_t (*write)(void *cookie, int32_t fd, const void *buffer,
                     uint32_t count);
};

struct canopus_client_v1 {
    const struct canopus_client_io_v1 *io;
    void *io_cookie;
    int32_t fd;
    uint8_t tx[CANOPUS_CLIENT_WIRE_CAPACITY];
    uint8_t rx[CANOPUS_CLIENT_WIRE_CAPACITY];
};

struct canopus_client_device_snapshot_v1 {
    uint32_t framework_revision;
    uint32_t safe_mode;
    uint32_t module_count;
    uint32_t safe_mode_reason;
    int32_t error_code;
    uint32_t flags;
};

struct canopus_client_module_snapshot_v1 {
    uint32_t slot;
    uint32_t state;
    uint32_t lifecycle_class;
    uint32_t version;
    uint32_t flags;
    char module_id[32];
};

int32_t canopus_client_init(struct canopus_client_v1 *client,
                            const struct canopus_client_io_v1 *io,
                            void *io_cookie);
int32_t canopus_client_open(struct canopus_client_v1 *client);
int32_t canopus_client_close(struct canopus_client_v1 *client);

/* Adapter matching canopus_manager_model_v1.transport. One CPC2 record is one
 * atomic character-device transfer; a short read/write fails closed. */
int canopus_client_transport(const struct canopus_proto_request_v1 *request,
                             const void *payload,
                             struct canopus_proto_response_v1 *response,
                             void *cookie);

int32_t canopus_client_query_device(
    struct canopus_client_v1 *client, uint32_t request_id,
    struct canopus_client_device_snapshot_v1 *snapshot);
int32_t canopus_client_query_module(
    struct canopus_client_v1 *client, uint32_t request_id, uint32_t slot,
    struct canopus_client_module_snapshot_v1 *snapshot);

/* libc/NuttX fd adapter. Kept separate from the portable core so freestanding
 * users may supply their own syscall table. */
const struct canopus_client_io_v1 *canopus_client_posix_io(void);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_CLIENT_H */
