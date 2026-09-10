#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-11-4.100.139"
#define CANOPUS_TARGET_FIRMWARE_VERSION "4.100.139"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "user-4.100.139-cn-202608280000"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x31, 0xce, 0x82, 0x25, 0x7f, 0x7c, 0x12, 0x79, \
      0x50, 0xdc, 0x50, 0x70, 0xb8, 0x63, 0x16, 0x73, \
      0x0c, 0xf4, 0x68, 0xa4, 0x1f, 0x0d, 0x00, 0x45, \
      0x59, 0xe4, 0x1e, 0x7d, 0x92, 0x3b, 0x2c, 0x74 }

#endif
