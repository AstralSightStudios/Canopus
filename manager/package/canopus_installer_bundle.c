/* Signed receipt validation shared by host tests and the exact-target platform. */
#include "canopus_installer_bundle.h"
#include "canopus_runtime.h"
#include "monocypher-ed25519.h"

_Static_assert(sizeof(struct canopus_install_receipt_v1) ==
                   CANOPUS_INSTALL_RECEIPT_SIZE,
               "install receipt wire size");
_Static_assert(__builtin_offsetof(struct canopus_install_receipt_v1, signature) ==
                   CANOPUS_INSTALL_SIGNED_SIZE,
               "install receipt signed prefix");

static int bytes_equal(const uint8_t *left, const uint8_t *right, uint32_t size)
{
    uint8_t diff = 0u;
    uint32_t i;
    for (i = 0; i < size; i++) {
        diff |= (uint8_t)(left[i] ^ right[i]);
    }
    return diff == 0u;
}

static int fixed_string_equal(const uint8_t *fixed, uint32_t capacity,
                              const char *expected)
{
    uint32_t i = 0u;
    if (expected == 0) return 0;
    while (i < capacity && expected[i] != '\0') {
        if (fixed[i] != (uint8_t)expected[i]) return 0;
        i++;
    }
    if (i == capacity || fixed[i] != 0u || expected[i] != '\0') return 0;
    for (i++; i < capacity; i++) {
        if (fixed[i] != 0u) return 0;
    }
    return 1;
}

int canopus_install_module_id_valid(
    const uint8_t module_id[CANOPUS_INSTALL_MODULE_ID_SIZE])
{
    uint32_t i;
    if (module_id == 0 || module_id[0] == 0u) return 0;
    for (i = 0; i < CANOPUS_INSTALL_MODULE_ID_SIZE; i++) {
        uint8_t c = module_id[i];
        if (c == 0u) {
            uint32_t j;
            for (j = i + 1u; j < CANOPUS_INSTALL_MODULE_ID_SIZE; j++) {
                if (module_id[j] != 0u) return 0;
            }
            return 1;
        }
        if (!((c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
              (c >= (uint8_t)'0' && c <= (uint8_t)'9') || c == (uint8_t)'_' ||
              c == (uint8_t)'-' || c == (uint8_t)'.')) {
            return 0;
        }
    }
    return 0;
}

int canopus_install_receipt_validate(
    const struct canopus_install_receipt_v1 *receipt,
    const char *target_id,
    const uint8_t firmware_sha256[CANOPUS_INSTALL_DIGEST_SIZE],
    const uint8_t public_key[32])
{
    if (receipt == 0 || firmware_sha256 == 0 || public_key == 0 ||
        receipt->magic != CANOPUS_INSTALL_RECEIPT_MAGIC ||
        receipt->version != CANOPUS_INSTALL_RECEIPT_VERSION ||
        receipt->header_size != CANOPUS_INSTALL_RECEIPT_SIZE ||
        receipt->flags != 0u || receipt->reserved != 0u ||
        receipt->artifact_size == 0u ||
        receipt->artifact_size > CANOPUS_INSTALL_ARTIFACT_MAX ||
        receipt->lifecycle_class > CANOPUS_LIFECYCLE_PATCH_REBOOT_REQUIRED ||
        !canopus_install_module_id_valid(receipt->module_id) ||
        !fixed_string_equal(receipt->target_id,
                            CANOPUS_INSTALL_TARGET_ID_SIZE, target_id) ||
        !bytes_equal(receipt->firmware_sha256, firmware_sha256,
                     CANOPUS_INSTALL_DIGEST_SIZE)) {
        return -1;
    }
    if (crypto_ed25519_check(receipt->signature, public_key,
                             (const uint8_t *)receipt,
                             CANOPUS_INSTALL_SIGNED_SIZE) != 0) {
        return -1;
    }
    return 0;
}
