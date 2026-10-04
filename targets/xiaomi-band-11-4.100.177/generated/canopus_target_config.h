#ifndef CANOPUS_TARGET_CONFIG_H
#define CANOPUS_TARGET_CONFIG_H

#include <stdint.h>

#define CANOPUS_TARGET_ID "xiaomi-band-11-4.100.177"
#define CANOPUS_TARGET_FIRMWARE_VERSION "4.100.177"
#define CANOPUS_TARGET_FIRMWARE_BUILD \
    "user-4.100.177-cn-202609241530"
#define CANOPUS_SUP_PLATFORM_COMPLETE 0
#define CANOPUS_SUP_CUSTOM_LOADER 1

/* Platform ABI incomplete: firmware call macros are withheld. */
#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID
#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \
    { 0xff, 0x74, 0xc6, 0xf4, 0x67, 0x52, 0x99, 0x63, \
      0xb6, 0xaf, 0xd4, 0x06, 0x69, 0x43, 0x7d, 0x8e, \
      0xc3, 0x9a, 0x02, 0xe4, 0x8c, 0x10, 0xd0, 0x89, \
      0x37, 0x39, 0x84, 0x7b, 0x3b, 0xa3, 0xdc, 0x2b }

#endif
