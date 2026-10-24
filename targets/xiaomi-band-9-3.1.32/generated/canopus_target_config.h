#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-9-3.1.32"
#define CANOPUS_TARGET_FIRMWARE_VERSION "3.1.32"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "N66NFC_3.1.32"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x9c, 0x02, 0xda, 0xb4, 0x02, 0x0b, 0x2c, 0xc9, \
      0x66, 0x6e, 0xe7, 0xd3, 0x4c, 0xf2, 0x7d, 0x31, \
      0x1b, 0x76, 0xaa, 0xdc, 0xec, 0x51, 0x9a, 0x38, \
      0x36, 0x1b, 0xbc, 0xbd, 0x94, 0xc5, 0x32, 0x64 }

#endif
