/* Host tests: native app descriptor + launcher ordered app list
 * (CAN-APP-004/008/011). */
#include "canopus_test.h"
#include "canopus_app.h"
#include "canopus_ordered_list.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"
#include <stddef.h>
#include <string.h>

/* ---- app descriptor (CAN-APP-008) ---------------------------------- */

static int32_t fake_on_create(const struct canopus_app_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

TEST(app_descriptor_check_ok)
{
    struct canopus_app_descriptor_v1 d;
    canopus_memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_major = CANOPUS_APP_ABI_MAJOR;
    d.abi_minor = CANOPUS_APP_ABI_MINOR;
    d.on_create = fake_on_create;
    CHECK(canopus_app_descriptor_check(&d) == 0);
}

TEST(app_descriptor_rejects_bad_header)
{
    struct canopus_app_descriptor_v1 d;
    canopus_memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_major = CANOPUS_APP_ABI_MAJOR;
    CHECK(canopus_app_descriptor_check(&d) == 0);
    d.struct_size = 0;
    CHECK(canopus_app_descriptor_check(&d) == -1);
    d.struct_size = sizeof(d);
    d.abi_major = 99;
    CHECK(canopus_app_descriptor_check(&d) == -1);
    d.abi_major = CANOPUS_APP_ABI_MAJOR;
    d.abi_minor = CANOPUS_APP_ABI_MINOR + 1; /* future minor rejected */
    CHECK(canopus_app_descriptor_check(&d) == -1);
    CHECK(canopus_app_descriptor_check(0) == -1);
}

TEST(app_descriptor_layout_parity)
{
    /* 12 + 32 + 32 + 32 = 108 fixed prefix; callbacks follow. */
    CHECK(offsetof(struct canopus_app_descriptor_v1, app_id) == 12);
    CHECK(offsetof(struct canopus_app_descriptor_v1, icon_ref) == 76);
#if UINTPTR_MAX == 0xffffffffu
    CHECK(offsetof(struct canopus_app_descriptor_v1, on_create) == 108);
    CHECK(sizeof(struct canopus_app_descriptor_v1) == 128);
#else
    CHECK(offsetof(struct canopus_app_descriptor_v1, on_create) == 112);
    CHECK(sizeof(struct canopus_app_descriptor_v1) == 152);
#endif
}

/* ---- app status record (CAN-APP-011) -------------------------------- */

TEST(app_status_write_publishes_versioned_record)
{
    struct canopus_status_writer_v1 w;
    uint8_t buf[32];
    canopus_status_writer_init(&w, buf, sizeof(buf));
    CHECK(canopus_app_status_write(&w, 3, 0x100) == 0);
    /* APP2 magic + abi major + state + flags = 16 bytes, little-endian */
    CHECK(w.used == 16);
    CHECK(CANOPUS_SNAPSHOT_READY(&w.snap));
    CHECK(buf[0] == 0x32 && buf[1] == 'P' && buf[2] == 'P' && buf[3] == 'A');
    CHECK(buf[4] == 1);  /* abi major */
    CHECK(buf[8] == 3);  /* app state */
    CHECK(buf[12] == 0x00 && buf[13] == 0x01); /* app flags 0x100 LE */
}

TEST(app_status_write_rejects_small_writer)
{
    struct canopus_status_writer_v1 w;
    uint8_t buf[8]; /* cannot fit 16 bytes */
    canopus_status_writer_init(&w, buf, sizeof(buf));
    /* the caller's writer is untouched on failure (a temp copy is used) */
    CHECK(canopus_app_status_write(&w, 3, 0) == -1);
    CHECK(w.used == 0);
    CHECK(w.dropped == 0);
}

/* ---- ordered app list (CAN-APP-004) --------------------------------- */

TEST(ordered_list_roundtrip)
{
    struct canopus_ordered_app_v1 apps[3];
    uint8_t wire[512];
    int n;
    canopus_memset(apps, 0, sizeof(apps));
    canopus_buf_copy(apps[0].app_name, sizeof(apps[0].app_name), "com.xiaomi.settings");
    apps[0].enabled = 1;
    apps[0].hidden = 0;
    canopus_buf_copy(apps[1].app_name, sizeof(apps[1].app_name), "com.canopus.manager");
    apps[1].enabled = 1;
    apps[1].hidden = 1;
    canopus_buf_copy(apps[2].app_name, sizeof(apps[2].app_name), "com.example.timer");
    apps[2].enabled = 0;
    apps[2].hidden = 0;

    n = canopus_ordered_list_serialize(apps, 3, wire, sizeof(wire));
    CHECK(n > 0);

    /* header: u16 count = 3 */
    CHECK(wire[0] == 3 && wire[1] == 0);

    /* entry 0 name offset must point at a valid string */
    {
        uint32_t off = (uint32_t)wire[2] | ((uint32_t)wire[3] << 8) |
                       ((uint32_t)wire[4] << 16) | ((uint32_t)wire[5] << 24);
        CHECK(strcmp((const char *)(wire + off), "com.xiaomi.settings") == 0);
        CHECK(wire[2 + 8] == 1);  /* enabled */
        CHECK(wire[2 + 9] == 0);  /* hidden */
    }

    /* entry 2 hidden bit */
    CHECK(wire[2 + 16 * 1 + 8] == 1);
    CHECK(wire[2 + 16 * 1 + 9] == 1);

    /* parse it back */
    {
        struct canopus_ordered_app_v1 back[3];
        int got = canopus_ordered_list_parse(wire, (uint32_t)n, back, 3);
        CHECK(got == 3);
        CHECK(strcmp(back[0].app_name, "com.xiaomi.settings") == 0);
        CHECK(back[0].enabled == 1 && back[0].hidden == 0);
        CHECK(strcmp(back[1].app_name, "com.canopus.manager") == 0);
        CHECK(back[1].enabled == 1 && back[1].hidden == 1);
        CHECK(strcmp(back[2].app_name, "com.example.timer") == 0);
        CHECK(back[2].enabled == 0 && back[2].hidden == 0);
    }
}

TEST(ordered_list_rejects_truncated)
{
    uint8_t wire[8] = { 2, 0, 0, 0, 0, 0, 0, 0 }; /* count=2 but no entries */
    struct canopus_ordered_app_v1 out[2];
    CHECK(canopus_ordered_list_parse(wire, sizeof(wire), out, 2) == -1);
}

TEST(ordered_list_rejects_bad_name_offset)
{
    uint8_t wire[64];
    canopus_memset(wire, 0, sizeof(wire));
    wire[0] = 1; wire[1] = 0;
    /* entry name offset = 1000 (out of bounds) */
    wire[2] = 0xE8; wire[3] = 0x03;
    struct canopus_ordered_app_v1 out[1];
    CHECK(canopus_ordered_list_parse(wire, sizeof(wire), out, 1) == -1);
}

TEST(ordered_list_rejects_unterminated_name)
{
    uint8_t wire[64];
    canopus_memset(wire, 0, sizeof(wire));
    wire[0] = 1; wire[1] = 0;
    wire[2] = 18; wire[3] = 0; /* name at offset 18 */
    /* fill every byte from 18 to the end of the buffer: no NUL in range */
    canopus_memset(wire + 18, 'x', sizeof(wire) - 18);
    struct canopus_ordered_app_v1 out[1];
    CHECK(canopus_ordered_list_parse(wire, sizeof(wire), out, 1) == -1);
}

TEST(ordered_list_rejects_oversized_count)
{
    uint8_t wire[64];
    canopus_memset(wire, 0, sizeof(wire));
    wire[0] = 0xFF; wire[1] = 0xFF; /* count 65535 > max */
    struct canopus_ordered_app_v1 out[1];
    CHECK(canopus_ordered_list_parse(wire, sizeof(wire), out, 1) == -1);
}

TEST(ordered_list_serialize_overflow)
{
    struct canopus_ordered_app_v1 app;
    uint8_t wire[8];
    canopus_memset(&app, 0, sizeof(app));
    canopus_buf_copy(app.app_name, sizeof(app.app_name), "a");
    CHECK(canopus_ordered_list_serialize(&app, 1, wire, sizeof(wire)) == -1);
}

static const struct test_registry app_sdk_tests[] = {
    { "app_descriptor_check_ok", app_descriptor_check_ok_wrapper },
    { "app_descriptor_rejects_bad_header", app_descriptor_rejects_bad_header_wrapper },
    { "app_descriptor_layout_parity", app_descriptor_layout_parity_wrapper },
    { "app_status_write_publishes_versioned_record", app_status_write_publishes_versioned_record_wrapper },
    { "app_status_write_rejects_small_writer", app_status_write_rejects_small_writer_wrapper },
    { "ordered_list_roundtrip", ordered_list_roundtrip_wrapper },
    { "ordered_list_rejects_truncated", ordered_list_rejects_truncated_wrapper },
    { "ordered_list_rejects_bad_name_offset", ordered_list_rejects_bad_name_offset_wrapper },
    { "ordered_list_rejects_unterminated_name", ordered_list_rejects_unterminated_name_wrapper },
    { "ordered_list_rejects_oversized_count", ordered_list_rejects_oversized_count_wrapper },
    { "ordered_list_serialize_overflow", ordered_list_serialize_overflow_wrapper },
};
#define APP_SDK_TESTS_LEN (sizeof(app_sdk_tests) / sizeof(app_sdk_tests[0]))

int run_app_sdk_tests(void)
{
    RUN_TESTS(app_sdk_tests, APP_SDK_TESTS_LEN);
}
