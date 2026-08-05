/* Host tests: ABI sizes and descriptor validation (CAN-C-001/002). */
#include "canopus_test.h"
#include "canopus_abi.h"
#include "canopus_runtime.h"
#include <string.h>

TEST(abi_fixed_layout)
{
    /* control header is all fixed-width: exactly 24 bytes */
    CHECK_EQ(sizeof(struct canopus_control_header_v1), 24u);
    /* pointer-independent prefix of the module descriptor */
    CHECK_EQ(offsetof(struct canopus_module_descriptor_v1, module_id), 12u);
    CHECK_EQ(offsetof(struct canopus_module_descriptor_v1, target_id), 92u);
    /* snapshot is a single u32 */
    CHECK_EQ(sizeof(struct canopus_snapshot_v1), 4u);
}

static int dummy_prepare(const struct canopus_context_v1 *c) { (void)c; return 0; }
static int dummy_activate(const struct canopus_context_v1 *c) { (void)c; return 0; }
static int dummy_deactivate(const struct canopus_context_v1 *c) { (void)c; return 0; }
static int dummy_stop(const struct canopus_context_v1 *c) { (void)c; return 0; }
static int dummy_query(struct canopus_status_writer_v1 *w) { (void)w; return 0; }

static void make_valid_descriptor(struct canopus_module_descriptor_v1 *d)
{
    memset(d, 0, sizeof(*d));
    d->struct_size = sizeof(*d);
    d->abi_major = CANOPUS_ABI_MAJOR;
    d->abi_minor = CANOPUS_ABI_MINOR;
    memcpy(d->module_id, "org.example.test", sizeof("org.example.test"));
    d->prepare = dummy_prepare;
    d->activate = dummy_activate;
    d->deactivate = dummy_deactivate;
    d->stop = dummy_stop;
    d->query = dummy_query;
}

TEST(descriptor_check)
{
    struct canopus_module_descriptor_v1 d;
    make_valid_descriptor(&d);
    CHECK(canopus_module_descriptor_check(&d) == 0);

    /* wrong ABI major is rejected */
    d.abi_major = 99;
    CHECK(canopus_module_descriptor_check(&d) == -1);
    d.abi_major = CANOPUS_ABI_MAJOR;

    /* truncated struct_size is rejected */
    d.struct_size = 16;
    CHECK(canopus_module_descriptor_check(&d) == -1);
    d.struct_size = sizeof(d);

    /* null descriptor rejected */
    CHECK(canopus_module_descriptor_check(0) == -1);
}

/* ---- CAN-P1-010: malformed descriptor table ------------------------ */

TEST(descriptor_check_rejects_unknown_flags)
{
    struct canopus_module_descriptor_v1 d;
    make_valid_descriptor(&d);
    d.flags = 0x8000u; /* unknown bit */
    CHECK(canopus_module_descriptor_check(&d) == -1);
    /* all known flag bits together are accepted */
    d.flags = CANOPUS_MODULE_FLAGS_KNOWN;
    CHECK(canopus_module_descriptor_check(&d) == 0);
}

TEST(descriptor_check_rejects_newer_minor)
{
    struct canopus_module_descriptor_v1 d;
    make_valid_descriptor(&d);
    d.abi_minor = CANOPUS_ABI_MINOR + 1u;
    CHECK(canopus_module_descriptor_check(&d) == -1);
    d.abi_minor = CANOPUS_ABI_MINOR;
    CHECK(canopus_module_descriptor_check(&d) == 0);
}

TEST(descriptor_check_rejects_oversized_struct)
{
    struct canopus_module_descriptor_v1 d;
    make_valid_descriptor(&d);
    d.struct_size = CANOPUS_MODULE_DESCRIPTOR_MAX_SIZE + 1u;
    CHECK(canopus_module_descriptor_check(&d) == -1);
}

TEST(descriptor_check_rejects_missing_callbacks)
{
    struct canopus_module_descriptor_v1 d;
    make_valid_descriptor(&d);
    d.prepare = 0;
    CHECK(canopus_module_descriptor_check(&d) == -1);
    make_valid_descriptor(&d);
    d.query = 0;
    CHECK(canopus_module_descriptor_check(&d) == -1);
}

TEST(descriptor_check_rejects_bad_identity_strings)
{
    struct canopus_module_descriptor_v1 d;
    /* empty module_id is rejected */
    make_valid_descriptor(&d);
    d.module_id[0] = '\0';
    CHECK(canopus_module_descriptor_check(&d) == -1);
    /* module_id without a NUL inside the fixed array is rejected */
    make_valid_descriptor(&d);
    memset(d.module_id, 'x', sizeof(d.module_id));
    CHECK(canopus_module_descriptor_check(&d) == -1);
    /* control characters in module_id are rejected */
    make_valid_descriptor(&d);
    memcpy(d.module_id, "org.example.test", sizeof("org.example.test"));
    d.module_id[3] = 0x01;
    CHECK(canopus_module_descriptor_check(&d) == -1);
    /* build_id without a NUL is rejected */
    make_valid_descriptor(&d);
    memset(d.build_id, 'y', sizeof(d.build_id));
    CHECK(canopus_module_descriptor_check(&d) == -1);
    /* target_id without a NUL is rejected */
    make_valid_descriptor(&d);
    memset(d.target_id, 'z', sizeof(d.target_id));
    CHECK(canopus_module_descriptor_check(&d) == -1);
}

static struct test_registry abi_tests[] = {
    { "abi_fixed_layout", abi_fixed_layout_wrapper },
    { "descriptor_check", descriptor_check_wrapper },
    { "descriptor_check_rejects_unknown_flags", descriptor_check_rejects_unknown_flags_wrapper },
    { "descriptor_check_rejects_newer_minor", descriptor_check_rejects_newer_minor_wrapper },
    { "descriptor_check_rejects_oversized_struct", descriptor_check_rejects_oversized_struct_wrapper },
    { "descriptor_check_rejects_missing_callbacks", descriptor_check_rejects_missing_callbacks_wrapper },
    { "descriptor_check_rejects_bad_identity_strings", descriptor_check_rejects_bad_identity_strings_wrapper },
};
#define ABI_TESTS_LEN (sizeof(abi_tests) / sizeof(abi_tests[0]))

int run_abi_tests(void)
{
    RUN_TESTS(abi_tests, ABI_TESTS_LEN);
}
