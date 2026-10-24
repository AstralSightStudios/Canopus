#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-10-3.1.28"
#define CANOPUS_TARGET_FIRMWARE_VERSION "3.1.28"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "9d8f394:nx_bestbsp_ap"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0xfb, 0x72, 0x64, 0x72, 0x41, 0x6e, 0x99, 0xba, \
      0x11, 0xd1, 0xc2, 0x0c, 0xe8, 0xf0, 0xd4, 0xf0, \
      0x83, 0x1a, 0x6e, 0x9a, 0x67, 0x3d, 0x63, 0xbb, \
      0x59, 0x18, 0xe2, 0xc3, 0x92, 0x1d, 0x88, 0x45 }

#endif
