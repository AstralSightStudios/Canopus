/*
 * canopus_control.c — sequence snapshot and status writer.
 *
 * Snapshot protocol: writer flips sequence to odd, writes fields, then
 * publishes the same even begin/end value. A reader only accepts a record
 * when begin == end and the sequence is even.
 */
#include "canopus_abi.h"
#include "canopus_memory.h"

void canopus_snapshot_begin(struct canopus_snapshot_v1 *snap)
{
    /* odd => mid-write */
    snap->sequence = snap->sequence | 1u;
}

void canopus_snapshot_commit(struct canopus_snapshot_v1 *snap)
{
    snap->sequence = snap->sequence | 1u;   /* ensure odd->even parity */
    snap->sequence = snap->sequence + 1u;   /* now even, same as begin */
}

int canopus_status_writer_init(struct canopus_status_writer_v1 *w,
                               uint8_t *buf, uint32_t capacity)
{
    if (w == 0 || buf == 0 || capacity == 0) {
        return -1;
    }
    w->buf = buf;
    w->capacity = capacity;
    w->used = 0;
    w->dropped = 0;
    w->snap.sequence = 0; /* even => initially valid/empty */
    return 0;
}

static int status_ensure(struct canopus_status_writer_v1 *w, uint32_t need)
{
    if (w->used + need <= w->capacity) {
        return 0;
    }
    w->dropped += 1;
    return -1;
}

int canopus_status_put_u8(struct canopus_status_writer_v1 *w, uint8_t v)
{
    if (status_ensure(w, 1) != 0) {
        return -1;
    }
    w->buf[w->used++] = v;
    return 0;
}

int canopus_status_put_u16(struct canopus_status_writer_v1 *w, uint16_t v)
{
    uint8_t tmp[2];
    tmp[0] = (uint8_t)(v & 0xff);
    tmp[1] = (uint8_t)((v >> 8) & 0xff);
    return canopus_status_put_bytes(w, tmp, 2);
}

int canopus_status_put_u32(struct canopus_status_writer_v1 *w, uint32_t v)
{
    uint8_t tmp[4];
    tmp[0] = (uint8_t)(v & 0xff);
    tmp[1] = (uint8_t)((v >> 8) & 0xff);
    tmp[2] = (uint8_t)((v >> 16) & 0xff);
    tmp[3] = (uint8_t)((v >> 24) & 0xff);
    return canopus_status_put_bytes(w, tmp, 4);
}

int canopus_status_put_bytes(struct canopus_status_writer_v1 *w,
                             const void *src, uint32_t len)
{
    if (status_ensure(w, len) != 0) {
        return -1;
    }
    if (len > 0 && src != 0) {
        canopus_memcpy(w->buf + w->used, src, len);
    }
    w->used += len;
    return 0;
}

void canopus_status_writer_publish(struct canopus_status_writer_v1 *w)
{
    canopus_snapshot_commit(&w->snap);
}
