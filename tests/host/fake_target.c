/*
 * fake_target.c — host fake target implementation (CAN-C-009).
 */
#include "fake_target.h"
#include "canopus_memory.h"
#include <string.h>

/* ---- allocator ---------------------------------------------------- */

#define FAKE_ARENA_SIZE (64u * 1024u)

struct fake_alloc_block {
    struct fake_alloc_block *next;
    size_t size;
};

static unsigned char g_arena[FAKE_ARENA_SIZE];
static size_t g_arena_used = 0;
static struct fake_alloc_block *g_blocks = 0;
static size_t g_live = 0;

static void *block_data(struct fake_alloc_block *b)
{
    return (unsigned char *)b + sizeof(*b);
}

void *fake_alloc(size_t n)
{
    struct fake_alloc_block *b;
    size_t total = sizeof(*b) + n;
    if (g_arena_used + total > FAKE_ARENA_SIZE) {
        return 0;
    }
    b = (struct fake_alloc_block *)(g_arena + g_arena_used);
    g_arena_used += total;
    b->size = n;
    b->next = g_blocks;
    g_blocks = b;
    g_live += 1;
    return block_data(b);
}

void fake_free(void *p)
{
    struct fake_alloc_block *b;
    if (p == 0) {
        return;
    }
    b = (struct fake_alloc_block *)((unsigned char *)p - sizeof(*b));
    /* simple validation: the block must be within the arena */
    if ((unsigned char *)b < g_arena ||
        (unsigned char *)b >= g_arena + FAKE_ARENA_SIZE) {
        return;
    }
    if (g_live > 0) {
        g_live -= 1;
    }
    (void)b;
}

size_t fake_alloc_live_count(void)
{
    return g_live;
}

void fake_alloc_reset(void)
{
    g_arena_used = 0;
    g_blocks = 0;
    g_live = 0;
}

/* ---- clock -------------------------------------------------------- */

int fake_clock_gettime(uint32_t clock_id, struct fake_timespec *ts)
{
    static uint32_t ticks = 0;
    if (ts == 0 || clock_id != 1u) {
        return -1;
    }
    ticks += 1000u; /* advance 1 ms per call */
    ts->tv_sec = (int32_t)(ticks / 1000u);
    ts->tv_nsec = (int32_t)((ticks % 1000u) * 1000000u);
    return 0;
}

/* ---- timer -------------------------------------------------------- */

struct fake_timer {
    fake_timer_cb cb;
    void *cookie;
    uint32_t period_ms;
    uint32_t due;
    int active;
};

static struct fake_timer g_timers[FAKE_TIMER_MAX];
static uint32_t g_now_ms = 0;

int fake_timer_register(fake_timer_cb cb, void *cookie, uint32_t period_ms)
{
    int i;
    if (cb == 0) {
        return -1;
    }
    for (i = 0; i < FAKE_TIMER_MAX; i++) {
        if (!g_timers[i].active) {
            g_timers[i].cb = cb;
            g_timers[i].cookie = cookie;
            g_timers[i].period_ms = period_ms;
            g_timers[i].due = g_now_ms + period_ms;
            g_timers[i].active = 1;
            return i;
        }
    }
    return -1;
}

int fake_timer_cancel(int timer_id)
{
    if (timer_id < 0 || timer_id >= FAKE_TIMER_MAX ||
        !g_timers[timer_id].active) {
        return -1;
    }
    g_timers[timer_id].active = 0;
    return 0;
}

void fake_timer_fire_due(void)
{
    int i;
    g_now_ms += 1u; /* advance one tick */
    for (i = 0; i < FAKE_TIMER_MAX; i++) {
        if (g_timers[i].active && g_now_ms >= g_timers[i].due) {
            g_timers[i].due = g_now_ms + g_timers[i].period_ms;
            g_timers[i].cb(g_timers[i].cookie);
        }
    }
}

size_t fake_timer_active_count(void)
{
    size_t n = 0;
    int i;
    for (i = 0; i < FAKE_TIMER_MAX; i++) {
        if (g_timers[i].active) {
            n++;
        }
    }
    return n;
}

/* ---- driver namespace --------------------------------------------- */

struct fake_driver {
    char name[FAKE_DRIVER_NAME_MAX];
    const void *ops;
    void *private_data;
    int held;
    int present;
};

static struct fake_driver g_drivers[FAKE_DRIVER_MAX];

int fake_driver_register(const char *name, const void *ops, void *private_data)
{
    int i;
    if (name == 0 || canopus_strlen(name) >= FAKE_DRIVER_NAME_MAX) {
        return -1;
    }
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (g_drivers[i].present && strcmp(g_drivers[i].name, name) == 0) {
            return -1; /* collision */
        }
    }
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (!g_drivers[i].present) {
            strcpy(g_drivers[i].name, name);
            g_drivers[i].ops = ops;
            g_drivers[i].private_data = private_data;
            g_drivers[i].held = 0;
            g_drivers[i].present = 1;
            return 0;
        }
    }
    return -1; /* full */
}

int fake_driver_unregister(const char *name)
{
    int i;
    if (name == 0) {
        return -1;
    }
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (g_drivers[i].present && strcmp(g_drivers[i].name, name) == 0) {
            if (g_drivers[i].held) {
                return -16; /* EBUSY: open refs still hold it */
            }
            g_drivers[i].present = 0;
            return 0;
        }
    }
    return -1; /* not found */
}

int fake_driver_hold(const char *name)
{
    int i;
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (g_drivers[i].present && strcmp(g_drivers[i].name, name) == 0) {
            g_drivers[i].held = 1;
            return 0;
        }
    }
    return -1;
}

int fake_driver_release(const char *name)
{
    int i;
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (g_drivers[i].present && strcmp(g_drivers[i].name, name) == 0) {
            g_drivers[i].held = 0;
            return 0;
        }
    }
    return -1;
}

size_t fake_driver_count(void)
{
    size_t n = 0;
    int i;
    for (i = 0; i < FAKE_DRIVER_MAX; i++) {
        if (g_drivers[i].present) {
            n++;
        }
    }
    return n;
}
