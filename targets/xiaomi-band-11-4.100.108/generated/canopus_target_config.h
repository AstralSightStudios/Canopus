#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-11-4.100.108"
#define CANOPUS_TARGET_FIRMWARE_VERSION "4.100.108"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "user-4.100.108-cn-202607230300"
#define CANOPUS_SUP_PLATFORM_COMPLETE 1

#define FW_VERSION_ADDRESS UINT32_C(0x0CA0044D)
#define FW_BUILD_ADDRESS UINT32_C(0x0CA004D6)

#define CANOPUS_SUP_NUTTX_RENAME UINT32_C(0x0C33D6C5)
#define CANOPUS_SUP_NUTTX_UNLINK UINT32_C(0x0C33CAF9)
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \
    canopus_fw_register_driver((path), (fops), (mode), (priv))
#define CANOPUS_SUP_UNREGISTER_DRIVER(path) canopus_fw_unregister_driver(path)
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x93, 0x15, 0xca, 0x35, 0x3f, 0x62, 0x4c, 0xec, \
      0x25, 0xdf, 0xcf, 0xc9, 0x8a, 0x95, 0xba, 0x95, \
      0x9e, 0x2d, 0x7b, 0x24, 0x57, 0x3b, 0xf1, 0xd6, \
      0xad, 0xf1, 0x6e, 0xa1, 0x03, 0x41, 0xbd, 0x99 }

#endif
