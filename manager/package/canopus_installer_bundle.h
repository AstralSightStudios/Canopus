/* canopus_installer_bundle.h — fixed signed module-install receipt. */
#ifndef CANOPUS_INSTALLER_BUNDLE_H
#define CANOPUS_INSTALLER_BUNDLE_H

#include <stdint.h>

#define CANOPUS_INSTALL_RECEIPT_MAGIC 0x31494D43u /* "CMI1" */
#define CANOPUS_INSTALL_RECEIPT_VERSION 1u
#define CANOPUS_INSTALL_RECEIPT_SIZE 256u
#define CANOPUS_INSTALL_SIGNED_SIZE 192u
#define CANOPUS_INSTALL_MODULE_ID_SIZE 32u
#define CANOPUS_INSTALL_TARGET_ID_SIZE 48u
#define CANOPUS_INSTALL_SIGNER_ID_SIZE 16u
#define CANOPUS_INSTALL_DIGEST_SIZE 32u
#define CANOPUS_INSTALL_SIGNATURE_SIZE 64u
/* Installer transport sanity bound, not a firmware modlib load limit. */
#define CANOPUS_INSTALL_ARTIFACT_MAX 393216u

struct canopus_install_receipt_v1 {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t flags;
    uint32_t lifecycle_class;
    uint32_t module_version;
    uint32_t artifact_size;
    uint32_t reserved;
    uint8_t module_id[CANOPUS_INSTALL_MODULE_ID_SIZE];
    uint8_t target_id[CANOPUS_INSTALL_TARGET_ID_SIZE];
    uint8_t firmware_sha256[CANOPUS_INSTALL_DIGEST_SIZE];
    uint8_t artifact_sha256[CANOPUS_INSTALL_DIGEST_SIZE];
    uint8_t signer_id[CANOPUS_INSTALL_SIGNER_ID_SIZE];
    uint8_t signature[CANOPUS_INSTALL_SIGNATURE_SIZE];
};

int canopus_install_receipt_validate(
    const struct canopus_install_receipt_v1 *receipt,
    const char *target_id,
    const uint8_t firmware_sha256[CANOPUS_INSTALL_DIGEST_SIZE],
    const uint8_t public_key[32]);
int canopus_install_module_id_valid(
    const uint8_t module_id[CANOPUS_INSTALL_MODULE_ID_SIZE]);

#endif
