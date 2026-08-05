/*
 * canopus_module.c — module descriptor validation and bounded buffer helper.
 */
#include "canopus_runtime.h"
#include "canopus_memory.h"

int canopus_buf_copy(char *dst, uint32_t capacity, const char *src)
{
    uint32_t n;
    if (dst == 0 || capacity == 0 || src == 0) {
        return -1;
    }
    n = (uint32_t)canopus_strlen(src);
    if (n >= capacity) {
        if (capacity > 1u) {
            canopus_memcpy(dst, src, capacity - 1u);
            dst[capacity - 1u] = '\0';
        } else {
            dst[0] = '\0';
        }
        return -1; /* truncated */
    }
    canopus_memcpy(dst, src, n);
    dst[n] = '\0';
    return (int)n;
}

/* ---- bounded text writer (CAN-P0-002) ------------------------------ */

int canopus_text_writer_init(struct canopus_text_writer_v1 *w,
                             char *buf, uint32_t cap)
{
    if (w == 0 || buf == 0 || cap == 0) {
        return -1;
    }
    w->buf = buf;
    w->cap = cap;
    w->used = 0;
    w->truncated = 0;
    w->buf[0] = '\0';
    return 0;
}

int canopus_text_writer_append(struct canopus_text_writer_v1 *w,
                               const char *s)
{
    uint32_t n, room;
    if (w == 0 || w->buf == 0 || w->cap == 0 || s == 0) {
        return -1;
    }
    if (w->truncated || w->used >= w->cap) {
        return CANOPUS_TEXT_TRUNCATED;
    }
    n = (uint32_t)canopus_strlen(s);
    room = w->cap - 1u - w->used; /* chars that fit before the NUL */
    if (n <= room) {
        canopus_memcpy(w->buf + w->used, s, n);
        w->used += n;
        w->buf[w->used] = '\0';
        return 0;
    }
    /* truncated: fill up to cap-1 and keep the record NUL-terminated */
    canopus_memcpy(w->buf + w->used, s, room);
    w->used = w->cap - 1u;
    w->buf[w->used] = '\0';
    w->truncated = 1;
    return CANOPUS_TEXT_TRUNCATED;
}

/* Validates the module descriptor header and identity fields (CAN-P1-010).
 * Returns 0 when the descriptor is well-formed for the current ABI. */
int canopus_module_descriptor_check(const struct canopus_module_descriptor_v1 *d)
{
    uint32_t i;
    uint32_t min_size;
    if (d == 0) {
        return -1;
    }
    /* append-only minor: struct_size must span the v1 callbacks and stay
     * within the bounded descriptor size; unknown trailing fields are
     * ignored, but an absurd size is a different layout and fails closed. */
    min_size = (uint32_t)offsetof(struct canopus_module_descriptor_v1, query) + 4u;
    if (d->struct_size < min_size ||
        d->struct_size > CANOPUS_MODULE_DESCRIPTOR_MAX_SIZE) {
        return -1;
    }
    if (d->abi_major != CANOPUS_ABI_MAJOR) {
        return -1;
    }
    /* a module compiled against a newer minor uses fields we do not know */
    if (d->abi_minor > CANOPUS_ABI_MINOR) {
        return -1;
    }
    /* flags: only known bits, no silent future semantics */
    if ((d->flags & ~CANOPUS_MODULE_FLAGS_KNOWN) != 0u) {
        return -1;
    }
    /* identity strings must be NUL-terminated inside their fixed arrays */
    for (i = 0; i < sizeof(d->module_id); i++) {
        if (d->module_id[i] == 0) {
            break;
        }
    }
    if (i == sizeof(d->module_id)) {
        return -1; /* no NUL in module_id */
    }
    for (i = 0; i < sizeof(d->module_version); i++) {
        if (d->module_version[i] == 0) {
            break;
        }
    }
    if (i == sizeof(d->module_version)) {
        return -1;
    }
    for (i = 0; i < sizeof(d->build_id); i++) {
        if (d->build_id[i] == 0) {
            break;
        }
    }
    if (i == sizeof(d->build_id)) {
        return -1;
    }
    for (i = 0; i < sizeof(d->target_id); i++) {
        if (d->target_id[i] == 0) {
            break;
        }
    }
    if (i == sizeof(d->target_id)) {
        return -1;
    }
    /* module_id is the key identity: non-empty and printable ASCII */
    if (d->module_id[0] == '\0') {
        return -1;
    }
    for (i = 0; i < sizeof(d->module_id) && d->module_id[i] != '\0'; i++) {
        if (d->module_id[i] < 0x20 || d->module_id[i] > 0x7E) {
            return -1;
        }
    }
    /* v1 requires all five callbacks in the fixed prefix. Whether each
     * pointer actually lands in the loaded artifact's executable section
     * is a loader-time check, not statable here. */
    if (d->prepare == 0 || d->activate == 0 || d->deactivate == 0 ||
        d->stop == 0 || d->query == 0) {
        return -1;
    }
    return 0;
}
