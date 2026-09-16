#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-11-4.100.155"
#define CANOPUS_TARGET_FIRMWARE_VERSION "4.100.155"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "user-4.100.155-cn-202609041500"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0xea, 0x0b, 0xdf, 0x19, 0x20, 0xcb, 0x30, 0x22, \
      0x3d, 0x61, 0x64, 0x32, 0xaf, 0x00, 0x56, 0x5c, \
      0xa6, 0x76, 0x22, 0xe6, 0x46, 0x83, 0x28, 0xf5, \
      0xea, 0xb1, 0x55, 0xf8, 0xcd, 0xc2, 0xfb, 0x9f }

#endif
