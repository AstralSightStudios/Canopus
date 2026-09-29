/* P65 dual-slot registry envelope; filesystem calls are injected. */
#include "canopus_manager_p65_registry.h"
#include "canopus_memory.h"

int canopus_manager_p65_registry_files_init(
    struct canopus_manager_p65_registry_files_v1 *files,
    const struct canopus_p65_registry_file_ops_v1 *ops, void *cookie,
    const char *slot0_path, const char *slot1_path)
{
    uint32_t i = 0u;
    if (files == 0) return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    canopus_memset(files, 0, sizeof(*files));
    if (ops == 0 || ops->open == 0 || ops->size == 0 || ops->read == 0 ||
        ops->write == 0 || ops->close == 0 || slot0_path == 0 ||
        slot1_path == 0 || slot0_path[0] == '\0' || slot1_path[0] == '\0') {
        return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    }
    while (slot0_path[i] != '\0' && slot0_path[i] == slot1_path[i]) i++;
    if (slot0_path[i] == slot1_path[i]) {
        return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    }
    files->ops = ops;
    files->cookie = cookie;
    files->paths[0] = slot0_path;
    files->paths[1] = slot1_path;
    return CANOPUS_P65_REGISTRY_OK;
}

static int p65_file_read_slot(void *cookie, uint32_t slot, uint8_t *buffer,
                              uint32_t capacity, uint32_t *actual_size)
{
    struct canopus_manager_p65_registry_files_v1 *files = cookie;
    uint32_t length = 0u, offset = 0u, limit;
    int handle, result = CANOPUS_P65_REGISTRY_ERR_IO;
    if (actual_size != 0) *actual_size = 0u;
    if (files == 0 || files->ops == 0 ||
        slot >= CANOPUS_P65_REGISTRY_SLOT_COUNT || buffer == 0 ||
        actual_size == 0) return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    handle = files->ops->open(files->cookie, files->paths[slot], 0);
    if (handle == -2) return CANOPUS_P65_REGISTRY_ABSENT;
    if (handle < 0) return CANOPUS_P65_REGISTRY_ERR_IO;
    if (files->ops->size(files->cookie, handle, &length) != 0) goto done;
    limit = length < capacity ? length : capacity;
    while (offset < limit) {
        int32_t count = files->ops->read(files->cookie, handle,
                                        buffer + offset, limit - offset);
        if (count <= 0 || (uint32_t)count > limit - offset) goto done;
        offset += (uint32_t)count;
    }
    *actual_size = length;
    result = CANOPUS_P65_REGISTRY_OK;
done:
    if (files->ops->close(files->cookie, handle) != 0)
        result = CANOPUS_P65_REGISTRY_ERR_IO;
    if (result != CANOPUS_P65_REGISTRY_OK) *actual_size = 0u;
    return result;
}

static int p65_file_write_slot(void *cookie, uint32_t slot,
                               const uint8_t *buffer, uint32_t size)
{
    struct canopus_manager_p65_registry_files_v1 *files = cookie;
    uint32_t offset = 0u;
    int handle, result = CANOPUS_P65_REGISTRY_ERR_IO;
    if (files == 0 || files->ops == 0 ||
        slot >= CANOPUS_P65_REGISTRY_SLOT_COUNT || buffer == 0 ||
        size != CANOPUS_P65_REGISTRY_RECORD_SIZE)
        return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    handle = files->ops->open(files->cookie, files->paths[slot], 1);
    if (handle < 0) return CANOPUS_P65_REGISTRY_ERR_IO;
    while (offset < size) {
        int32_t count = files->ops->write(files->cookie, handle,
                                         buffer + offset, size - offset);
        if (count <= 0 || (uint32_t)count > size - offset) goto done;
        offset += (uint32_t)count;
    }
    result = CANOPUS_P65_REGISTRY_OK;
done:
    if (files->ops->close(files->cookie, handle) != 0)
        result = CANOPUS_P65_REGISTRY_ERR_IO;
    return result;
}

const struct canopus_p65_registry_io_v1 canopus_manager_p65_registry_file_io = {
    p65_file_read_slot, p65_file_write_slot,
};

#define P65_REGISTRY_HEADER_MAGIC 0u
#define P65_REGISTRY_HEADER_VERSION 4u
#define P65_REGISTRY_HEADER_SEQUENCE 8u
#define P65_REGISTRY_HEADER_PAYLOAD_SIZE 12u
#define P65_REGISTRY_HEADER_CRC 16u
#define P65_REGISTRY_PAYLOAD_OFFSET CANOPUS_P65_REGISTRY_ENVELOPE_SIZE

enum p65_slot_state {
    P65_SLOT_MISSING = 0,
    P65_SLOT_VALID,
    P65_SLOT_CORRUPT,
};

struct p65_slot_info {
    uint32_t sequence;
    uint32_t crc;
    enum p65_slot_state state;
};

static uint32_t p65_read_u32(const uint8_t *buffer, uint32_t offset)
{
    return (uint32_t)buffer[offset] |
           ((uint32_t)buffer[offset + 1u] << 8) |
           ((uint32_t)buffer[offset + 2u] << 16) |
           ((uint32_t)buffer[offset + 3u] << 24);
}

static void p65_write_u32(uint8_t *buffer, uint32_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t)value;
    buffer[offset + 1u] = (uint8_t)(value >> 8);
    buffer[offset + 2u] = (uint8_t)(value >> 16);
    buffer[offset + 3u] = (uint8_t)(value >> 24);
}

static uint32_t p65_crc32_byte(uint32_t crc, uint8_t value)
{
    uint32_t bit;
    crc ^= value;
    for (bit = 0u; bit < 8u; bit++) {
        uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
        crc = (crc >> 1) ^ (0xEDB88320u & mask);
    }
    return crc;
}

static uint32_t p65_record_crc(const uint8_t *record)
{
    uint32_t crc = 0xFFFFFFFFu;
    uint32_t i;
    for (i = 0u; i < P65_REGISTRY_HEADER_CRC; i++) {
        crc = p65_crc32_byte(crc, record[i]);
    }
    for (i = 0u; i < CANOPUS_SUP_REGISTRY_SIZE; i++) {
        crc = p65_crc32_byte(crc,
                             record[P65_REGISTRY_PAYLOAD_OFFSET + i]);
    }
    return ~crc;
}

static int p65_registry_payload_valid(const uint8_t *data, uint32_t len)
{
    if (data == 0 || len != CANOPUS_SUP_REGISTRY_SIZE ||
        p65_read_u32(data, 0u) != CANOPUS_SUP_REGISTRY_MAGIC ||
        p65_read_u32(data, 4u) != CANOPUS_SUP_REGISTRY_VERSION ||
        p65_read_u32(data, 8u) > CANOPUS_SUP_MODULE_SLOTS) {
        return 0;
    }
    return 1;
}

static int p65_record_valid(const uint8_t *record, uint32_t len)
{
    if (record == 0 || len != CANOPUS_P65_REGISTRY_RECORD_SIZE ||
        p65_read_u32(record, P65_REGISTRY_HEADER_MAGIC) !=
            CANOPUS_P65_REGISTRY_MAGIC ||
        p65_read_u32(record, P65_REGISTRY_HEADER_VERSION) !=
            CANOPUS_P65_REGISTRY_VERSION ||
        p65_read_u32(record, P65_REGISTRY_HEADER_SEQUENCE) == 0u ||
        p65_read_u32(record, P65_REGISTRY_HEADER_PAYLOAD_SIZE) !=
            CANOPUS_SUP_REGISTRY_SIZE ||
        p65_read_u32(record, P65_REGISTRY_HEADER_CRC) !=
            p65_record_crc(record) ||
        !p65_registry_payload_valid(record + P65_REGISTRY_PAYLOAD_OFFSET,
                                    CANOPUS_SUP_REGISTRY_SIZE)) {
        return 0;
    }
    return 1;
}

static int p65_slot_read(struct canopus_manager_p65_registry_store_v1 *store,
                         uint32_t slot, struct p65_slot_info *info)
{
    uint32_t actual_size = 0u;
    int rc;

    info->sequence = 0u;
    info->crc = 0u;
    info->state = P65_SLOT_MISSING;
    rc = store->io->read_slot(store->io_cookie, slot, store->scratch,
                              CANOPUS_P65_REGISTRY_RECORD_SIZE,
                              &actual_size);
    if (rc == 1) {
        return 0;
    }
    if (rc != 0) {
        return CANOPUS_P65_REGISTRY_ERR_IO;
    }
    if (!p65_record_valid(store->scratch, actual_size)) {
        info->state = P65_SLOT_CORRUPT;
        return 0;
    }
    info->sequence = p65_read_u32(store->scratch,
                                  P65_REGISTRY_HEADER_SEQUENCE);
    info->crc = p65_read_u32(store->scratch, P65_REGISTRY_HEADER_CRC);
    info->state = P65_SLOT_VALID;
    return 0;
}

static int p65_bytes_equal(const uint8_t *left, const uint8_t *right,
                           uint32_t size)
{
    uint8_t diff = 0u;
    uint32_t i;
    for (i = 0u; i < size; i++) {
        diff |= (uint8_t)(left[i] ^ right[i]);
    }
    return diff == 0u;
}

static int p65_store_error(struct canopus_manager_p65_registry_store_v1 *store,
                           int error)
{
    store->last_error = error;
    return error;
}

static int p65_overlaps_scratch(
    const struct canopus_manager_p65_registry_store_v1 *store,
    const void *buffer, uint32_t size)
{
    uintptr_t buffer_start = (uintptr_t)buffer;
    uintptr_t scratch_start = (uintptr_t)store->scratch;
    uintptr_t buffer_end;
    uintptr_t scratch_end = scratch_start + sizeof(store->scratch);

    if (size == 0u) return 0;
    if (buffer_start > UINTPTR_MAX - (uintptr_t)size) return 1;
    buffer_end = buffer_start + (uintptr_t)size;
    return buffer_start < scratch_end && scratch_start < buffer_end;
}

int canopus_manager_p65_registry_store_init(
    struct canopus_manager_p65_registry_store_v1 *store,
    const struct canopus_p65_registry_io_v1 *io, void *io_cookie)
{
    if (store == 0 || io == 0 || io->read_slot == 0 ||
        io->write_slot == 0) {
        return CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    }
    canopus_memset(store, 0, sizeof(*store));
    store->io = io;
    store->io_cookie = io_cookie;
    store->last_error = CANOPUS_P65_REGISTRY_OK;
    return CANOPUS_P65_REGISTRY_OK;
}

int canopus_manager_p65_registry_persist(void *cookie,
                                         const uint8_t *data, uint32_t len)
{
    struct canopus_manager_p65_registry_store_v1 *store =
        (struct canopus_manager_p65_registry_store_v1 *)cookie;
    struct p65_slot_info slot_info[CANOPUS_P65_REGISTRY_SLOT_COUNT];
    uint32_t valid_count = 0u;
    uint32_t latest_slot = 0u;
    uint32_t target_slot = 0u;
    uint32_t next_sequence = 1u;
    uint32_t slot;
    uint32_t crc;
    uint32_t actual_size = 0u;
    int rc;

    if (store == 0 || store->io == 0 || data == 0 ||
        len != CANOPUS_SUP_REGISTRY_SIZE ||
        p65_overlaps_scratch(store, data, len) ||
        !p65_registry_payload_valid(data, len)) {
        return store != 0
                   ? p65_store_error(store,
                                     CANOPUS_P65_REGISTRY_ERR_ARGUMENT)
                   : CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    }
    for (slot = 0u; slot < CANOPUS_P65_REGISTRY_SLOT_COUNT; slot++) {
        rc = p65_slot_read(store, slot, &slot_info[slot]);
        if (rc != 0) {
            return p65_store_error(store, rc);
        }
        if (slot_info[slot].state == P65_SLOT_VALID) {
            if (valid_count == 0u ||
                slot_info[slot].sequence >
                    slot_info[latest_slot].sequence) {
                latest_slot = slot;
            }
            valid_count++;
        }
    }

    if (valid_count == 1u) {
        target_slot = 1u - latest_slot;
        if (slot_info[latest_slot].sequence == UINT32_MAX) {
            return p65_store_error(
                store, CANOPUS_P65_REGISTRY_ERR_SEQUENCE);
        }
        next_sequence = slot_info[latest_slot].sequence + 1u;
    } else if (valid_count == 2u) {
        uint32_t other_slot = 1u - latest_slot;
        if (slot_info[0].sequence == slot_info[1].sequence &&
            slot_info[0].crc != slot_info[1].crc) {
            return p65_store_error(store,
                                   CANOPUS_P65_REGISTRY_ERR_SEQUENCE);
        }
        target_slot = other_slot;
        if (slot_info[latest_slot].sequence == UINT32_MAX) {
            return p65_store_error(
                store, CANOPUS_P65_REGISTRY_ERR_SEQUENCE);
        }
        next_sequence = slot_info[latest_slot].sequence + 1u;
    }

    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_MAGIC,
                  CANOPUS_P65_REGISTRY_MAGIC);
    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_VERSION,
                  CANOPUS_P65_REGISTRY_VERSION);
    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_SEQUENCE,
                  next_sequence);
    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_PAYLOAD_SIZE,
                  CANOPUS_SUP_REGISTRY_SIZE);
    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_CRC, 0u);
    canopus_memcpy(store->scratch + P65_REGISTRY_PAYLOAD_OFFSET, data, len);
    crc = p65_record_crc(store->scratch);
    p65_write_u32(store->scratch, P65_REGISTRY_HEADER_CRC, crc);

    rc = store->io->write_slot(store->io_cookie, target_slot,
                               store->scratch,
                               CANOPUS_P65_REGISTRY_RECORD_SIZE);
    if (rc != 0) {
        return p65_store_error(store, CANOPUS_P65_REGISTRY_ERR_WRITE);
    }
    rc = store->io->read_slot(store->io_cookie, target_slot, store->scratch,
                              CANOPUS_P65_REGISTRY_RECORD_SIZE,
                              &actual_size);
    if (rc != 0 || !p65_record_valid(store->scratch, actual_size) ||
        p65_read_u32(store->scratch, P65_REGISTRY_HEADER_SEQUENCE) !=
            next_sequence ||
        p65_read_u32(store->scratch, P65_REGISTRY_HEADER_CRC) != crc ||
        !p65_bytes_equal(store->scratch + P65_REGISTRY_PAYLOAD_OFFSET, data,
                         len)) {
        return p65_store_error(store, CANOPUS_P65_REGISTRY_ERR_VERIFY);
    }
    store->last_slot = target_slot;
    store->last_sequence = next_sequence;
    return p65_store_error(store, CANOPUS_P65_REGISTRY_OK);
}

int canopus_manager_p65_registry_restore(void *cookie,
                                         uint8_t *data, uint32_t len)
{
    struct canopus_manager_p65_registry_store_v1 *store =
        (struct canopus_manager_p65_registry_store_v1 *)cookie;
    struct p65_slot_info slot_info[CANOPUS_P65_REGISTRY_SLOT_COUNT];
    uint32_t valid_count = 0u;
    uint32_t chosen_slot = 0u;
    uint32_t slot;
    int rc;

    if (store == 0 || store->io == 0 || data == 0 ||
        len != CANOPUS_SUP_REGISTRY_SIZE ||
        p65_overlaps_scratch(store, data, len)) {
        return store != 0
                   ? p65_store_error(store,
                                     CANOPUS_P65_REGISTRY_ERR_ARGUMENT)
                   : CANOPUS_P65_REGISTRY_ERR_ARGUMENT;
    }
    for (slot = 0u; slot < CANOPUS_P65_REGISTRY_SLOT_COUNT; slot++) {
        rc = p65_slot_read(store, slot, &slot_info[slot]);
        if (rc != 0) {
            return p65_store_error(store, rc);
        }
        if (slot_info[slot].state == P65_SLOT_VALID) {
            if (valid_count == 0u ||
                slot_info[slot].sequence >
                    slot_info[chosen_slot].sequence) {
                chosen_slot = slot;
            }
            valid_count++;
        }
    }
    if (valid_count == 0u) {
        if (slot_info[0].state == P65_SLOT_MISSING &&
            slot_info[1].state == P65_SLOT_MISSING) {
            store->last_error = CANOPUS_P65_REGISTRY_ABSENT;
            return CANOPUS_P65_REGISTRY_ABSENT;
        }
        return p65_store_error(store, CANOPUS_P65_REGISTRY_ERR_FORMAT);
    }
    if (valid_count == 2u &&
        slot_info[0].sequence == slot_info[1].sequence &&
        slot_info[0].crc != slot_info[1].crc) {
        return p65_store_error(store, CANOPUS_P65_REGISTRY_ERR_SEQUENCE);
    }

    /* Re-read the chosen slot because scratch contains the last slot scanned. */
    {
        uint32_t actual_size = 0u;
        rc = store->io->read_slot(store->io_cookie, chosen_slot,
                                  store->scratch,
                                  CANOPUS_P65_REGISTRY_RECORD_SIZE,
                                  &actual_size);
        if (rc != 0 || !p65_record_valid(store->scratch, actual_size) ||
            p65_read_u32(store->scratch,
                         P65_REGISTRY_HEADER_SEQUENCE) !=
                slot_info[chosen_slot].sequence) {
            return p65_store_error(store, CANOPUS_P65_REGISTRY_ERR_VERIFY);
        }
        canopus_memcpy(data,
                       store->scratch + P65_REGISTRY_PAYLOAD_OFFSET, len);
    }
    store->last_slot = chosen_slot;
    store->last_sequence = slot_info[chosen_slot].sequence;
    return p65_store_error(store, CANOPUS_P65_REGISTRY_OK);
}
