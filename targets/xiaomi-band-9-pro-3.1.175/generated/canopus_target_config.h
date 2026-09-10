#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-9-pro-3.1.175"
#define CANOPUS_TARGET_FIRMWARE_VERSION "3.1.175"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "CONBINE_LTALM054_T1175_04141021_release_5793"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x4f, 0x43, 0xb3, 0x25, 0xad, 0xdd, 0x6d, 0x9e, \
      0x6e, 0x7c, 0x7e, 0x2a, 0x4d, 0x00, 0xff, 0xe3, \
      0xf2, 0x3d, 0x5f, 0xb1, 0x56, 0x0d, 0x8f, 0xe5, \
      0x03, 0x54, 0x40, 0x02, 0xac, 0x1f, 0x51, 0x6b }

#endif
