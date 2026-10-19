/* canopus_installer_bundle.h — fixed unsigned module-install receipt. */
#ifndef CANOPUS_INSTALLER_BUNDLE_H
#define CANOPUS_INSTALLER_BUNDLE_H

#include <stdint.h>

#define CANOPUS_INSTALL_RECEIPT_MAGIC 0x32494D43u /* "CMI2" */
#define CANOPUS_INSTALL_RECEIPT_VERSION 2u
#define CANOPUS_INSTALL_RECEIPT_SIZE 176u
#define CANOPUS_INSTALL_MODULE_ID_SIZE 32u
#define CANOPUS_INSTALL_TARGET_ID_SIZE 48u
#define CANOPUS_INSTALL_DIGEST_SIZE 32u
/* Installer transport sanity bound, not a firmware modlib load limit. */
#define CANOPUS_INSTALL_ARTIFACT_MAX (4u * 1024u * 1024u)

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
};

int canopus_install_receipt_validate(
    const struct canopus_install_receipt_v1 *receipt,
    const char *target_id,
    const uint8_t firmware_sha256[CANOPUS_INSTALL_DIGEST_SIZE]);
int canopus_install_module_id_valid(
    const uint8_t module_id[CANOPUS_INSTALL_MODULE_ID_SIZE]);

#endif
