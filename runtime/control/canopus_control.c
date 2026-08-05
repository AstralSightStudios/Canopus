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

/* CAN-P1-004: every public entry point re-validates the writer and the
 * offset before touching the buffer; `need <= capacity - used` replaces the
 * overflow-prone `used + need <= capacity`; a NULL source with non-zero
 * length fails without advancing `used`; `dropped` saturates; begin/publish
 * form an explicit lifecycle and publish requires the WRITING state. */

static int status_writable(const struct canopus_status_writer_v1 *w)
{
    return w != 0 && w->buf != 0 && w->capacity != 0 &&
           w->state == CANOPUS_STATUS_WRITER_WRITING &&
           w->used <= w->capacity;
}

static int status_ensure(struct canopus_status_writer_v1 *w, uint32_t need)
{
    if (need <= w->capacity - w->used) { /* used <= capacity (checked above) */
        return 0;
    }
    if (w->dropped < CANOPUS_STATUS_WRITER_DROPPED_MAX) {
        w->dropped += 1u;
    }
    return -1;
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
    w->state = CANOPUS_STATUS_WRITER_WRITING; /* first record auto-begins */
    w->snap.sequence = 0;
    canopus_snapshot_begin(&w->snap); /* odd => not valid until publish */
    return 0;
}

int canopus_status_writer_begin(struct canopus_status_writer_v1 *w)
{
    if (w == 0 || w->buf == 0 || w->capacity == 0 ||
        w->state != CANOPUS_STATUS_WRITER_PUBLISHED) {
        return -1;
    }
    w->used = 0;
    w->state = CANOPUS_STATUS_WRITER_WRITING;
    canopus_snapshot_begin(&w->snap);
    return 0;
}

int canopus_status_put_u8(struct canopus_status_writer_v1 *w, uint8_t v)
{
    if (!status_writable(w)) {
        return -1;
    }
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
    if (!status_writable(w)) {
        return -1;
    }
    if (len > 0 && src == 0) {
        return -1; /* NULL source must not advance `used` */
    }
    if (status_ensure(w, len) != 0) {
        return -1;
    }
    if (len > 0) {
        canopus_memcpy(w->buf + w->used, src, len);
    }
    w->used += len;
    return 0;
}

int canopus_status_writer_publish(struct canopus_status_writer_v1 *w)
{
    if (!status_writable(w)) {
        return -1; /* double publish / publish without a writing generation */
    }
    canopus_snapshot_commit(&w->snap);
    w->state = CANOPUS_STATUS_WRITER_PUBLISHED;
    return 0;
}
