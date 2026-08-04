/*
 * canopus_ordered_list.h — launcher ordered app list, host parser/serializer
 * (CAN-APP-004 partial, based on EVID-APP-003).
 *
 * Recovered wire format (EVID-APP-003, protobuf_set_ordered_app_list):
 *   u16 count, then `count` entries of 16 bytes each:
 *     +0   u32 name    — offset from the buffer base to the NUL-terminated
 *                        app name (the on-device struct stores a pointer that
 *                        the loader resolves; the host form uses an offset)
 *     +4   u32 pad
 *     +8   u8  enabled (0/1)
 *     +9   u8  hidden  (bit0)
 *     +10  u8[6] pad
 *
 * The parser/serializer here is the host reference implementation; the device
 * launcher adapter resolves the same layout from firmware memory.
 */
#ifndef CANOPUS_ORDERED_LIST_H
#define CANOPUS_ORDERED_LIST_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_ORDERED_LIST_ENTRY_SIZE 16u
#define CANOPUS_ORDERED_LIST_MAX_ENTRIES 64u
#define CANOPUS_ORDERED_LIST_NAME_MAX 64u

struct canopus_ordered_app_v1 {
    char app_name[CANOPUS_ORDERED_LIST_NAME_MAX];
    uint8_t enabled;
    uint8_t hidden;
};

/* Parses a wire buffer into `out`. Returns the number of entries parsed, or
 * -1 on malformed input (bad count, truncated entries, name offset out of
 * bounds, or a name not NUL-terminated inside the buffer). */
int canopus_ordered_list_parse(const uint8_t *buf, uint32_t len,
                               struct canopus_ordered_app_v1 *out,
                               uint32_t out_cap);

/* Serializes the canonical wire form into `buf` (capacity `cap`). Names are
 * appended after the entries and referenced by offset. Returns the number of
 * bytes written, or -1 when it does not fit or a name is too long. */
int canopus_ordered_list_serialize(const struct canopus_ordered_app_v1 *apps,
                                   uint32_t count,
                                   uint8_t *buf, uint32_t cap);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_ORDERED_LIST_H */
