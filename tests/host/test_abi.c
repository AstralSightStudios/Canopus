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

TEST(descriptor_check)
{
    struct canopus_module_descriptor_v1 d;
    memset(&d, 0, sizeof(d));
    d.struct_size = sizeof(d);
    d.abi_major = CANOPUS_ABI_MAJOR;
    d.abi_minor = CANOPUS_ABI_MINOR;
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

static struct test_registry abi_tests[] = {
    { "abi_fixed_layout", abi_fixed_layout_wrapper },
    { "descriptor_check", descriptor_check_wrapper },
};
#define ABI_TESTS_LEN (sizeof(abi_tests) / sizeof(abi_tests[0]))

int run_abi_tests(void)
{
    RUN_TESTS(abi_tests, ABI_TESTS_LEN);
}
