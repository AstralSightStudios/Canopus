#ifndef CANOPUS_MODULE_REGISTRATION_H
#define CANOPUS_MODULE_REGISTRATION_H

#include <stdint.h>

#define CANOPUS_MODULE_REGISTRATION_MAGIC 0x31524d43u /* "CMR1" */
#define CANOPUS_MODULE_REGISTRATION_SIZE 40u

struct canopus_module_registration_v1 {
    uint32_t magic;
    uint32_t descriptor;
    uint8_t module_id[32];
};

static inline int canopus_module_registration_is_frame(const void *buffer,
                                                        uint32_t count)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    return bytes != 0 && count == CANOPUS_MODULE_REGISTRATION_SIZE &&
           bytes[0] == (uint8_t)CANOPUS_MODULE_REGISTRATION_MAGIC &&
           bytes[1] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 8) &&
           bytes[2] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 16) &&
           bytes[3] == (uint8_t)(CANOPUS_MODULE_REGISTRATION_MAGIC >> 24);
}

#endif /* CANOPUS_MODULE_REGISTRATION_H */
