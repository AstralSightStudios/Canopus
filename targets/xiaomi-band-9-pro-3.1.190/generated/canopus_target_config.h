#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-9-pro-3.1.190"
#define CANOPUS_TARGET_FIRMWARE_VERSION "3.1.190"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "CONBINE_LTALM054_T1190_09161923_release_6006"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0x3e, 0x26, 0x4b, 0xc2, 0x67, 0x43, 0x1e, 0xc7, \
      0x18, 0x43, 0xcc, 0x7c, 0x2e, 0x4a, 0xcd, 0x92, \
      0x8b, 0x92, 0x80, 0xea, 0x39, 0xee, 0x1c, 0xd8, \
      0x88, 0x63, 0x6a, 0xdc, 0xeb, 0xab, 0xf8, 0x2f }

#endif
