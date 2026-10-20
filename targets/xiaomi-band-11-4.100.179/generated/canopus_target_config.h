#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-11-4.100.179"
#define CANOPUS_TARGET_FIRMWARE_VERSION "4.100.179"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "user-4.100.179-cn-202609302100"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x30, 0x7a, 0xda, 0xbe, 0x0f, 0xf6, 0x1c, 0x77, \
      0xbe, 0x73, 0xd0, 0x45, 0xa8, 0x26, 0x0e, 0x27, \
      0x9c, 0xdd, 0xd1, 0x31, 0xc7, 0xa5, 0x35, 0xa2, \
      0x67, 0x8e, 0xb3, 0x41, 0x99, 0x27, 0x8a, 0x2b }

#endif
