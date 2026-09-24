/* Host tests for the injected P65 dual-slot Supervisor registry store. */
#include <string.h>
#include "canopus_test.h"
#include "canopus_manager_p65_registry.h"
#include "canopus_supervisor_platform.h"

struct fake_p65_registry_io {
    uint8_t slots[CANOPUS_P65_REGISTRY_SLOT_COUNT]
                  [CANOPUS_P65_REGISTRY_RECORD_SIZE];
    uint32_t sizes[CANOPUS_P65_REGISTRY_SLOT_COUNT];
    uint8_t present[CANOPUS_P65_REGISTRY_SLOT_COUNT];
    int32_t fail_read_slot;
    int32_t fail_write_slot;
    int32_t partial_write_slot;
};

static int fake_p65_read_slot(void *cookie, uint32_t slot, uint8_t *buffer,
                              uint32_t capacity, uint32_t *actual_size)
{
    struct fake_p65_registry_io *io =
        (struct fake_p65_registry_io *)cookie;
    uint32_t copy_size;
    if (io == 0 || slot >= CANOPUS_P65_REGISTRY_SLOT_COUNT ||
        buffer == 0 || actual_size == 0) {
        return -1;
    }
    if (io->fail_read_slot == (int32_t)slot) return -1;
    if (!io->present[slot]) {
        *actual_size = 0u;
        return 1;
    }
    *actual_size = io->sizes[slot];
    copy_size = io->sizes[slot] < capacity ? io->sizes[slot] : capacity;
    if (copy_size != 0u) {
        memcpy(buffer, io->slots[slot], copy_size);
    }
    return 0;
}

static int fake_p65_write_slot(void *cookie, uint32_t slot,
                               const uint8_t *buffer, uint32_t size)
{
    struct fake_p65_registry_io *io =
        (struct fake_p65_registry_io *)cookie;
    uint32_t copy_size = size;
    if (io == 0 || slot >= CANOPUS_P65_REGISTRY_SLOT_COUNT ||
        buffer == 0 || size > CANOPUS_P65_REGISTRY_RECORD_SIZE) {
        return -1;
    }
    if (io->fail_write_slot == (int32_t)slot) return -1;
    if (io->partial_write_slot == (int32_t)slot) copy_size = size / 2u;
    memcpy(io->slots[slot], buffer, copy_size);
    io->sizes[slot] = copy_size;
    io->present[slot] = 1u;
    return 0;
}

static const struct canopus_p65_registry_io_v1 fake_p65_registry_ops = {
    fake_p65_read_slot,
    fake_p65_write_slot,
};

static const struct canopus_sup_platform_v1 p65_registry_platform = {
    "xiaomi-p65-3.100.043", 0, 0, 0, 0, 0,
    canopus_manager_p65_registry_persist,
    canopus_manager_p65_registry_restore,
};

static void fake_registry_init(struct fake_p65_registry_io *io)
{
    memset(io, 0, sizeof(*io));
    io->fail_read_slot = -1;
    io->fail_write_slot = -1;
    io->partial_write_slot = -1;
}

static void make_registry(uint8_t data[CANOPUS_SUP_REGISTRY_SIZE],
                          uint8_t marker)
{
    uint32_t i;
    memset(data, 0, CANOPUS_SUP_REGISTRY_SIZE);
    data[0] = (uint8_t)CANOPUS_SUP_REGISTRY_MAGIC;
    data[1] = (uint8_t)(CANOPUS_SUP_REGISTRY_MAGIC >> 8);
    data[2] = (uint8_t)(CANOPUS_SUP_REGISTRY_MAGIC >> 16);
    data[3] = (uint8_t)(CANOPUS_SUP_REGISTRY_MAGIC >> 24);
    data[4] = (uint8_t)CANOPUS_SUP_REGISTRY_VERSION;
    data[8] = 0u; /* module_count */
    for (i = 12u; i < CANOPUS_SUP_REGISTRY_HEADER; i++) {
        data[i] = marker;
    }
}

static uint32_t fake_u32(const uint8_t *data, uint32_t offset)
{
    return (uint32_t)data[offset] |
           ((uint32_t)data[offset + 1u] << 8) |
           ((uint32_t)data[offset + 2u] << 16) |
           ((uint32_t)data[offset + 3u] << 24);
}

static int bytes_equal(const uint8_t *a, const uint8_t *b, uint32_t len)
{
    return memcmp(a, b, len) == 0;
}

TEST(p65_registry_initial_absent_then_roundtrip)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t source[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t restored[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(source, 0x11u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io),
             CANOPUS_P65_REGISTRY_OK);
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)),
             CANOPUS_P65_REGISTRY_ABSENT);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, source,
                                                  sizeof(source)),
             CANOPUS_P65_REGISTRY_OK);
    CHECK_EQ(store.last_slot, 0u);
    CHECK_EQ(store.last_sequence, 1u);
    CHECK_EQ(io.sizes[0], CANOPUS_P65_REGISTRY_RECORD_SIZE);
    CHECK_EQ(fake_u32(io.slots[0], 8u), 1u);
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)),
             CANOPUS_P65_REGISTRY_OK);
    CHECK(bytes_equal(source, restored, sizeof(source)));
    CHECK_EQ(store.last_slot, 0u);
    CHECK_EQ(store.last_sequence, 1u);
}

TEST(p65_registry_second_save_uses_inactive_slot)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t first[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t second[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t third[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t restored[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(first, 0x21u);
    make_registry(second, 0x42u);
    make_registry(third, 0x73u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, first,
                                                  sizeof(first)), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, second,
                                                  sizeof(second)), 0);
    CHECK_EQ(fake_u32(io.slots[0], 8u), 1u);
    CHECK_EQ(fake_u32(io.slots[1], 8u), 2u);
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)), 0);
    CHECK(bytes_equal(second, restored, sizeof(second)));
    CHECK_EQ(store.last_slot, 1u);
    CHECK_EQ(store.last_sequence, 2u);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, third,
                                                  sizeof(third)), 0);
    CHECK_EQ(fake_u32(io.slots[0], 8u), 3u);
    CHECK_EQ(fake_u32(io.slots[1], 8u), 2u);
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)), 0);
    CHECK(bytes_equal(third, restored, sizeof(third)));
    CHECK_EQ(store.last_slot, 0u);
    CHECK_EQ(store.last_sequence, 3u);
}

TEST(p65_registry_corrupt_newest_falls_back_to_previous)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t first[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t second[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t restored[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(first, 0x31u);
    make_registry(second, 0x52u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, first,
                                                  sizeof(first)), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, second,
                                                  sizeof(second)), 0);
    io.slots[1][CANOPUS_P65_REGISTRY_ENVELOPE_SIZE + 12u] ^= 0x80u;
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)), 0);
    CHECK(bytes_equal(first, restored, sizeof(first)));
    CHECK_EQ(store.last_slot, 0u);
    CHECK_EQ(store.last_sequence, 1u);
}

TEST(p65_registry_torn_write_preserves_last_valid_slot)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t first[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t second[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t restored[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(first, 0x13u);
    make_registry(second, 0x64u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, first,
                                                  sizeof(first)), 0);
    io.partial_write_slot = 1;
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, second,
                                                  sizeof(second)),
             CANOPUS_P65_REGISTRY_ERR_VERIFY);
    io.partial_write_slot = -1;
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)), 0);
    CHECK(bytes_equal(first, restored, sizeof(first)));
}

TEST(p65_registry_fails_closed_on_corruption_or_io_error)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t data[CANOPUS_SUP_REGISTRY_SIZE];
    uint8_t restored[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(data, 0x17u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, data,
                                                  sizeof(data)), 0);
    io.slots[0][CANOPUS_P65_REGISTRY_ENVELOPE_SIZE] ^= 0x01u;
    io.present[1] = 1u;
    io.sizes[1] = CANOPUS_P65_REGISTRY_RECORD_SIZE;
    memset(io.slots[1], 0, sizeof(io.slots[1]));
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)),
             CANOPUS_P65_REGISTRY_ERR_FORMAT);
    io.fail_read_slot = 1;
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, restored,
                                                  sizeof(restored)),
             CANOPUS_P65_REGISTRY_ERR_IO);
}

TEST(p65_registry_hooks_roundtrip_supervisor_metadata)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    struct canopus_supervisor_v1 first;
    struct canopus_supervisor_v1 restored;
    fake_registry_init(&io);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    CHECK_EQ(canopus_supervisor_init(&first, 7u, &p65_registry_platform,
                                     &store), 0);
    CHECK_EQ(canopus_supervisor_add_module(
                 &first, CANOPUS_LIFECYCLE_REMOVABLE, 3u, 1u,
                 "module.p65.test"), 0);
    first.modules[0].intent = CANOPUS_SUP_INTENT_ENABLED;
    first.modules[0].activation_error = 0x42u;
    CHECK_EQ(canopus_supervisor_save_registry(&first), 0);
    CHECK_EQ(canopus_supervisor_init(&restored, 7u,
                                     &p65_registry_platform, &store), 0);
    CHECK_EQ(canopus_supervisor_restore_registry(&restored), 0);
    CHECK_EQ(restored.module_count, 1u);
    CHECK(strcmp((const char *)restored.modules[0].module_id,
                 "module.p65.test") == 0);
    CHECK_EQ(restored.modules[0].intent, CANOPUS_SUP_INTENT_ENABLED);
    CHECK_EQ(restored.modules[0].activation_error, 0x42u);
}

TEST(p65_registry_rejects_bad_registry_payload)
{
    struct fake_p65_registry_io io;
    struct canopus_manager_p65_registry_store_v1 store;
    uint8_t data[CANOPUS_SUP_REGISTRY_SIZE];
    fake_registry_init(&io);
    make_registry(data, 0x19u);
    CHECK_EQ(canopus_manager_p65_registry_store_init(
                 &store, &fake_p65_registry_ops, &io), 0);
    data[0] = 0u;
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, data,
                                                  sizeof(data)),
             CANOPUS_P65_REGISTRY_ERR_ARGUMENT);
    make_registry(data, 0x19u);
    data[8] = (uint8_t)(CANOPUS_SUP_MODULE_SLOTS + 1u);
    CHECK_EQ(canopus_manager_p65_registry_persist(&store, data,
                                                  sizeof(data)),
             CANOPUS_P65_REGISTRY_ERR_ARGUMENT);
    CHECK_EQ(canopus_manager_p65_registry_restore(&store, store.scratch,
                                                  CANOPUS_SUP_REGISTRY_SIZE),
             CANOPUS_P65_REGISTRY_ERR_ARGUMENT);
}

static const struct test_registry p65_registry_store_tests[] = {
    { "p65_registry_initial_absent_then_roundtrip",
      p65_registry_initial_absent_then_roundtrip_wrapper },
    { "p65_registry_second_save_uses_inactive_slot",
      p65_registry_second_save_uses_inactive_slot_wrapper },
    { "p65_registry_corrupt_newest_falls_back_to_previous",
      p65_registry_corrupt_newest_falls_back_to_previous_wrapper },
    { "p65_registry_torn_write_preserves_last_valid_slot",
      p65_registry_torn_write_preserves_last_valid_slot_wrapper },
    { "p65_registry_fails_closed_on_corruption_or_io_error",
      p65_registry_fails_closed_on_corruption_or_io_error_wrapper },
    { "p65_registry_hooks_roundtrip_supervisor_metadata",
      p65_registry_hooks_roundtrip_supervisor_metadata_wrapper },
    { "p65_registry_rejects_bad_registry_payload",
      p65_registry_rejects_bad_registry_payload_wrapper },
};

int run_p65_registry_store_tests(void)
{
    RUN_TESTS(p65_registry_store_tests,
              sizeof(p65_registry_store_tests) /
                  sizeof(p65_registry_store_tests[0]));
}
