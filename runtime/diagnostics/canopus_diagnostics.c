/*
 * canopus_diagnostics.c — bounded append-only event writer.
 *
 * When the ring is full the oldest entry is overwritten and `dropped`
 * (saturating) counts the eviction. `next_sequence` is monotonic and is
 * NOT the number of stored entries; `stored_count` is. Readers detect
 * eviction between two reads with canopus_event_log_is_gap(). The writer
 * never blocks.
 */
#include "canopus_runtime.h"
#include "canopus_memory.h"

#define CANOPUS_EVENT_DROPPED_MAX 0xFFFFFFFFu

void canopus_event_log_init(struct canopus_event_log_v1 *log, uint32_t boot_id)
{
    uint32_t i;
    canopus_memset(log, 0, sizeof(*log));
    for (i = 0; i < CANOPUS_EVENT_LOG_ENTRIES; i++) {
        log->entries[i].boot_id = boot_id;
    }
    log->stored_count = 0;
}

uint32_t canopus_event_log_append(struct canopus_event_log_v1 *log,
                                  uint32_t timestamp,
                                  uint32_t module_id,
                                  uint32_t request_id,
                                  uint32_t module_gen,
                                  uint32_t state_before,
                                  uint32_t state_after,
                                  uint32_t result)
{
    struct canopus_event_v1 *e;
    if (log == 0) {
        return 0;
    }
    if (log->stored_count >= CANOPUS_EVENT_LOG_ENTRIES) {
        /* ring full: overwrite the oldest entry and count the eviction */
        if (log->dropped < CANOPUS_EVENT_DROPPED_MAX) {
            log->dropped += 1u;
        }
    } else {
        log->stored_count += 1u;
    }
    e = &log->entries[log->head];
    e->sequence = log->next_sequence;
    e->timestamp = timestamp;
    e->module_id = module_id;
    e->request_id = request_id;
    e->module_gen = module_gen;
    e->state_before = state_before;
    e->state_after = state_after;
    e->result = result;
    e->flags = 0;
    log->next_sequence += 1u;
    log->head = (log->head + 1u) % CANOPUS_EVENT_LOG_ENTRIES;
    return e->sequence;
}

uint32_t canopus_event_log_count(const struct canopus_event_log_v1 *log)
{
    return log == 0 ? 0 : log->stored_count;
}

uint32_t canopus_event_log_next_sequence(const struct canopus_event_log_v1 *log)
{
    return log == 0 ? 0 : log->next_sequence;
}

uint32_t canopus_event_log_dropped(const struct canopus_event_log_v1 *log)
{
    return log == 0 ? 0 : log->dropped;
}

int canopus_event_log_is_gap(uint32_t after, uint32_t candidate)
{
    /* exactly one successor in the monotonic sequence, modular */
    return (uint32_t)(candidate - after) != 1u;
}
