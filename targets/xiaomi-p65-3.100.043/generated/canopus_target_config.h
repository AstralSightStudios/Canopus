#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-p65-3.100.043"
#define CANOPUS_TARGET_FIRMWARE_VERSION "3.100.043"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "d516af0:nx_best1502p_ap"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0xa1, 0x9b, 0x60, 0x14, 0x77, 0x56, 0x97, 0x65, \
      0x10, 0x3e, 0xaf, 0x46, 0x8b, 0x93, 0x7b, 0xd8, \
      0x02, 0x6b, 0x4d, 0x68, 0xff, 0xa5, 0xc8, 0x96, \
      0xbd, 0xe2, 0x22, 0xa7, 0x9e, 0x47, 0x58, 0x58 }

#endif
