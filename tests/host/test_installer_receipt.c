/* Host tests for fixed unsigned CMI2 module-install receipts. */
#include "canopus_test.h"
#include "canopus_installer_bundle.h"
#include "canopus_memory.h"

static const uint8_t s_firmware_sha256[32] = {
    0xf7, 0x01, 0xa8, 0x4f, 0xfc, 0xaf, 0xa6, 0x7f,
    0x4d, 0x46, 0x03, 0xad, 0x8c, 0xd6, 0x6a, 0x11,
    0xe5, 0x44, 0x2f, 0x27, 0x14, 0x0f, 0x5a, 0xf0,
    0x98, 0x2e, 0x09, 0x75, 0xdc, 0xcd, 0x22, 0x5b,
};

static void load_receipt(struct canopus_install_receipt_v1 *receipt)
{
    canopus_memset(receipt, 0, sizeof(*receipt));
    receipt->magic = CANOPUS_INSTALL_RECEIPT_MAGIC;
    receipt->version = CANOPUS_INSTALL_RECEIPT_VERSION;
    receipt->header_size = CANOPUS_INSTALL_RECEIPT_SIZE;
    receipt->lifecycle_class = 0u;
    receipt->module_version = 1u;
    receipt->artifact_size = 0x516cu;
    canopus_memcpy(receipt->module_id, "canopus_hello", 13u);
    canopus_memcpy(receipt->target_id, "xiaomi-band-10-pro-3.101.030", 28u);
    canopus_memcpy(receipt->firmware_sha256, s_firmware_sha256, 32u);
}

static int validate(const struct canopus_install_receipt_v1 *receipt)
{
    return canopus_install_receipt_validate(
        receipt, "xiaomi-band-10-pro-3.101.030", s_firmware_sha256);
}

TEST(installer_receipt_accepts_valid_receipt)
{
    struct canopus_install_receipt_v1 receipt;
    load_receipt(&receipt);
    CHECK(validate(&receipt) == 0);
}

TEST(installer_receipt_rejects_mutations)
{
    struct canopus_install_receipt_v1 receipt;

    load_receipt(&receipt);
    receipt.target_id[0] ^= 1u;
    CHECK(validate(&receipt) == -1);

    load_receipt(&receipt);
    receipt.firmware_sha256[31] ^= 1u;
    CHECK(validate(&receipt) == -1);

    load_receipt(&receipt);
    receipt.magic = 0x31494D43u; /* legacy signed CMI1 */
    CHECK(validate(&receipt) == -1);

    load_receipt(&receipt);
    receipt.module_id[0] = (uint8_t)'/';
    CHECK(validate(&receipt) == -1);

    load_receipt(&receipt);
    receipt.artifact_size = CANOPUS_INSTALL_ARTIFACT_MAX + 1u;
    CHECK(validate(&receipt) == -1);
}

static struct test_registry installer_receipt_tests[] = {
    { "installer_receipt_accepts_valid_receipt",
      installer_receipt_accepts_valid_receipt_wrapper },
    { "installer_receipt_rejects_mutations",
      installer_receipt_rejects_mutations_wrapper },
};

int run_installer_receipt_tests(void)
{
    RUN_TESTS(installer_receipt_tests,
              sizeof(installer_receipt_tests) /
                  sizeof(installer_receipt_tests[0]));
}
