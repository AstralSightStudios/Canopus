/* canopus_ordered_list.c — launcher ordered app list host parser/serializer
 * (CAN-APP-004 partial, EVID-APP-003). */
#include "canopus_ordered_list.h"
#include "canopus_runtime.h"
#include "canopus_memory.h"

/* CAN-P1-009: names are scanned with a bounded window, never with an
 * unbounded canopus_strlen; name offsets must point at the name region
 * (>= 2 + count*16) and never into the header or the entry table; a failed
 * parse leaves the output fully zeroed. */

static const char *name_at(const uint8_t *buf, uint32_t len, uint32_t off)
{
    uint32_t i;
    if (off >= len) {
        return 0;
    }
    /* must be NUL-terminated inside the buffer and not too long */
    for (i = 0; i < CANOPUS_ORDERED_LIST_NAME_MAX; i++) {
        if (off + i >= len) {
            return 0;
        }
        if (buf[off + i] == 0) {
            return (const char *)(buf + off);
        }
    }
    return 0;
}

int canopus_ordered_list_parse(const uint8_t *buf, uint32_t len,
                               struct canopus_ordered_app_v1 *out,
                               uint32_t out_cap)
{
    uint32_t count, i, name_base;
    const uint8_t *p;
    if (buf == 0 || len < 2) {
        return -1;
    }
    count = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8);
    if (count > CANOPUS_ORDERED_LIST_MAX_ENTRIES) {
        return -1;
    }
    if (count > out_cap) {
        return -1;
    }
    if (count > 0 && out == 0) {
        return -1; /* non-empty result requires an output array */
    }
    /* never leave half-initialized output behind a failed parse: zero the
     * (bounded) output before any check that could fail midway. */
    if (out != 0 && out_cap > 0) {
        uint32_t z = out_cap < CANOPUS_ORDERED_LIST_MAX_ENTRIES
                         ? out_cap
                         : CANOPUS_ORDERED_LIST_MAX_ENTRIES;
        canopus_memset(out, 0, (size_t)z * sizeof(out[0]));
    }
    /* 2 bytes header + count * 16 entry bytes must fit */
    if ((uint64_t)2 + (uint64_t)count * CANOPUS_ORDERED_LIST_ENTRY_SIZE > len) {
        return -1;
    }
    /* the canonical writer places names after the entries; a name offset
     * into the header or the entry table is rejected */
    name_base = 2u + count * CANOPUS_ORDERED_LIST_ENTRY_SIZE;
    p = buf + 2;
    for (i = 0; i < count; i++) {
        uint32_t off = (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                       ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
        const char *name;
        if (off < name_base) {
            return -1; /* points into header/entry table, not a name */
        }
        name = name_at(buf, len, off);
        if (name == 0) {
            return -1;
        }
        canopus_buf_copy(out[i].app_name, sizeof(out[i].app_name), name);
        out[i].enabled = (uint8_t)(p[8] != 0u);
        out[i].hidden = (uint8_t)(p[9] & 1u);
        p += CANOPUS_ORDERED_LIST_ENTRY_SIZE;
    }
    return (int)count;
}

int canopus_ordered_list_serialize(const struct canopus_ordered_app_v1 *apps,
                                   uint32_t count,
                                   uint8_t *buf, uint32_t cap)
{
    uint32_t i, name_off;
    uint64_t needed;
    uint8_t *p;
    if (count > CANOPUS_ORDERED_LIST_MAX_ENTRIES) {
        return -1;
    }
    if (count > 0 && apps == 0) {
        return -1; /* non-empty input requires an apps array */
    }
    if (buf == 0 || cap < 2) {
        return -1;
    }
    needed = 2 + (uint64_t)count * CANOPUS_ORDERED_LIST_ENTRY_SIZE;
    for (i = 0; i < count; i++) {
        uint32_t nlen =
            (uint32_t)canopus_strnlen(apps[i].app_name,
                                      CANOPUS_ORDERED_LIST_NAME_MAX);
        if (nlen >= CANOPUS_ORDERED_LIST_NAME_MAX) {
            return -1; /* unterminated or too-long fixed array */
        }
        needed += nlen + 1;
    }
    if (needed > cap) {
        return -1;
    }

    buf[0] = (uint8_t)(count & 0xff);
    buf[1] = (uint8_t)((count >> 8) & 0xff);
    p = buf + 2;

    name_off = 2 + count * CANOPUS_ORDERED_LIST_ENTRY_SIZE;
    for (i = 0; i < count; i++) {
        uint32_t nlen =
            (uint32_t)canopus_strnlen(apps[i].app_name,
                                      CANOPUS_ORDERED_LIST_NAME_MAX);
        if (nlen >= CANOPUS_ORDERED_LIST_NAME_MAX) {
            return -1;
        }
        p[0] = (uint8_t)(name_off & 0xff);
        p[1] = (uint8_t)((name_off >> 8) & 0xff);
        p[2] = (uint8_t)((name_off >> 16) & 0xff);
        p[3] = (uint8_t)((name_off >> 24) & 0xff);
        p[4] = p[5] = p[6] = p[7] = 0;
        p[8] = apps[i].enabled ? 1u : 0u;
        p[9] = apps[i].hidden & 1u;
        p[10] = p[11] = p[12] = p[13] = p[14] = p[15] = 0;
        /* write name into its slot */
        canopus_memcpy(buf + name_off, apps[i].app_name, nlen);
        buf[name_off + nlen] = 0;
        name_off += nlen + 1;
        p += CANOPUS_ORDERED_LIST_ENTRY_SIZE;
    }
    return (int)needed;
}
