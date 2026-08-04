/*
 * canopus_resource.c — general resource tracker and callback generation
 * guards (architecture §10.4/§10.5, CAN-C-006/CAN-C-007).
 */
#include "canopus_runtime.h"
#include "canopus_memory.h"

void canopus_tracker_init(struct canopus_resource_tracker_v1 *t)
{
    canopus_memset(t, 0, sizeof(*t));
    t->generation = 1u;
}

static struct canopus_resource_v1 *find_slot(struct canopus_resource_tracker_v1 *t,
                                             void *handle)
{
    uint32_t i;
    for (i = 0; i < t->count; i++) {
        if (t->slots[i].handle == handle) {
            return &t->slots[i];
        }
    }
    return 0;
}

int canopus_tracker_add(struct canopus_resource_tracker_v1 *t,
                        const struct canopus_resource_v1 *res)
{
    if (t == 0 || res == 0 || res->handle == 0) {
        return -1;
    }
    if (t->count >= CANOPUS_RESOURCE_MAX) {
        return -1; /* table full */
    }
    if (find_slot(t, res->handle) != 0) {
        return -1; /* duplicate handle */
    }
    t->slots[t->count] = *res;
    t->slots[t->count].generation = t->generation;
    t->count += 1;
    return 0;
}

int canopus_tracker_drain(struct canopus_resource_tracker_v1 *t, void *handle)
{
    struct canopus_resource_v1 *r = find_slot(t, handle);
    if (r == 0) {
        return -1;
    }
    if (r->state != CANOPUS_RES_ACTIVE) {
        return -1;
    }
    r->state = CANOPUS_RES_DRAINING;
    r->generation += 1u;
    return 0;
}

int canopus_tracker_detach(struct canopus_resource_tracker_v1 *t, void *handle)
{
    struct canopus_resource_v1 *r = find_slot(t, handle);
    if (r == 0) {
        return -1;
    }
    if (r->state == CANOPUS_RES_RELEASED) {
        return -1;
    }
    /* DETACHED is terminal for this boot: the namespace entry is gone and
     * a blind retry would spin. */
    r->state = CANOPUS_RES_DETACHED;
    r->generation += 1u;
    return 0;
}

int canopus_tracker_release(struct canopus_resource_tracker_v1 *t, void *handle)
{
    struct canopus_resource_v1 *r = find_slot(t, handle);
    if (r == 0) {
        return -1;
    }
    if (r->state == CANOPUS_RES_RELEASED) {
        return -1; /* double free */
    }
    if (r->state == CANOPUS_RES_RETAINED_UNTIL_REBOOT) {
        return -1; /* not releasable this boot */
    }
    if (r->state == CANOPUS_RES_DETACHED) {
        return -1; /* namespace already gone; must not release or retry */
    }
    if (r->on_release != 0) {
        r->on_release(r);
    }
    r->state = CANOPUS_RES_RELEASED;
    r->generation += 1u;
    return 0;
}

int canopus_tracker_retain_until_reboot(struct canopus_resource_tracker_v1 *t,
                                        void *handle)
{
    struct canopus_resource_v1 *r = find_slot(t, handle);
    if (r == 0) {
        return -1;
    }
    if (r->state == CANOPUS_RES_RELEASED) {
        return -1;
    }
    r->state = CANOPUS_RES_RETAINED_UNTIL_REBOOT;
    r->generation += 1u;
    return 0;
}

void canopus_tracker_release_all(struct canopus_resource_tracker_v1 *t)
{
    /* release in reverse registration order so dependencies unwind */
    while (t->count > 0u) {
        struct canopus_resource_v1 *r = &t->slots[t->count - 1u];
        if (r->state != CANOPUS_RES_RELEASED &&
            r->state != CANOPUS_RES_RETAINED_UNTIL_REBOOT &&
            r->state != CANOPUS_RES_DETACHED) {
            /* DETACHED: namespace gone, in-flight refs may exist; do not
             * release blindly. Drop the record so the table drains. */
            if (r->on_release != 0) {
                r->on_release(r);
            }
            r->state = CANOPUS_RES_RELEASED;
        }
        t->count -= 1u;
        t->generation += 1u;
    }
}

uint32_t canopus_tracker_generation(const struct canopus_resource_tracker_v1 *t)
{
    return t->generation;
}

/* ------------------------------------------------------------------ */
/* Generation guards                                                   */
/* ------------------------------------------------------------------ */

void canopus_generation_init(struct canopus_generation_v1 *g)
{
    g->value = 1u;
}

void canopus_generation_bump(struct canopus_generation_v1 *g)
{
    g->value += 1u;
    if (g->value == 0u) {
        g->value = 1u; /* never wrap to the sentinel */
    }
}

uint32_t canopus_generation_get(const struct canopus_generation_v1 *g)
{
    return g->value;
}

int canopus_generation_valid(const struct canopus_generation_v1 *g,
                             uint32_t captured)
{
    return g->value == captured && g->value != 0u ? 1 : 0;
}
