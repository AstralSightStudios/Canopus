/*
 * canopus_diagnostics.c — bounded append-only event writer.
 *
 * When the ring is full the oldest entry is overwritten and `dropped`
 * counts the eviction. The writer never blocks.
 */
#include "canopus_runtime.h"
#include "canopus_memory.h"

void canopus_event_log_init(struct canopus_event_log_v1 *log, uint32_t boot_id)
{
    uint32_t i;
    canopus_memset(log, 0, sizeof(*log));
    for (i = 0; i < CANOPUS_EVENT_LOG_ENTRIES; i++) {
        log->entries[i].boot_id = boot_id;
    }
}

uint32_t canopus_event_log_append(struct canopus_event_log_v1 *log,
                                  uint32_t module_gen,
                                  uint32_t state_before,
                                  uint32_t state_after,
                                  uint32_t result)
{
    struct canopus_event_v1 *e;
    if (log == 0) {
        return 0;
    }
    e = &log->entries[log->head];
    e->sequence = log->next_sequence;
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
    return log->next_sequence;
}

uint32_t canopus_event_log_dropped(const struct canopus_event_log_v1 *log)
{
    return log->dropped;
}
