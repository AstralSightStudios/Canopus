/*
 * hello.c — C example removable module.
 *
 * Demonstrates the portable runtime on a real module shape: descriptor,
 * lifecycle, resource tracker, status writer, stale-callback guard. No
 * firmware addresses are hard-coded; platform hooks come from
 * hello_platform.h. Host-testable end to end.
 */
#include "canopus_abi.h"
#include "canopus_runtime.h"
#include "hello_platform.h"
#include <stddef.h>

#define HELLO_MAGIC 0x48454C4Fu /* "HELO" */
#define HELLO_STATUS_VERSION 1u

struct hello_state {
    struct canopus_lifecycle_v1 lc;
    struct canopus_resource_tracker_v1 tracker;
    struct canopus_generation_v1 gen;
    uint32_t gen_captured;
    int timer_id;
    uint32_t timer_fires;
    struct canopus_status_writer_v1 writer;
    uint8_t status_buf[32];
};

static struct hello_state s_state;

static void on_timer(void *cookie)
{
    struct hello_state *st = (struct hello_state *)cookie;
    /* Stale-callback guard: if the module was deactivated/stopped, the
     * captured generation no longer matches and this fire is a no-op. */
    if (!canopus_generation_valid(&st->gen, st->gen_captured)) {
        return;
    }
    st->timer_fires += 1u;
}

static int hello_prepare(const struct canopus_context_v1 *ctx)
{
    struct hello_state *st = &s_state;
    (void)ctx;
    if (canopus_lifecycle_init(&st->lc, CANOPUS_LIFECYCLE_REMOVABLE) != 0) {
        return -1;
    }
    canopus_tracker_init(&st->tracker);
    canopus_generation_init(&st->gen);
    st->gen_captured = 0;
    st->timer_id = -1;
    st->timer_fires = 0;
    if (canopus_status_writer_init(&st->writer, st->status_buf,
                                   sizeof(st->status_buf)) != 0) {
        return -1;
    }
    return 0;
}

static int hello_activate(const struct canopus_context_v1 *ctx)
{
    struct hello_state *st = &s_state;
    struct canopus_resource_v1 timer_res;
    (void)ctx;

    /* capture generation so the timer callback is valid only for this
     * activation */
    canopus_generation_bump(&st->gen);
    st->gen_captured = canopus_generation_get(&st->gen);

    st->timer_id = hello_timer_register(on_timer, st, 10u);
    if (st->timer_id < 0) {
        return -1;
    }

    timer_res.kind = CANOPUS_RESOURCE_TIMER;
    timer_res.state = CANOPUS_RES_ACTIVE;
    timer_res.generation = 0;
    timer_res.handle = (void *)(uintptr_t)(st->timer_id + 1);
    timer_res.on_release = 0;
    if (canopus_tracker_add(&st->tracker, &timer_res) != 0) {
        hello_timer_cancel(st->timer_id);
        st->timer_id = -1;
        return -1;
    }

    return 0;
}

static int hello_deactivate(const struct canopus_context_v1 *ctx)
{
    struct hello_state *st = &s_state;
    (void)ctx;
    if (st->timer_id >= 0) {
        hello_timer_cancel(st->timer_id);
        st->timer_id = -1;
    }
    /* invalidate any in-flight timer callback */
    canopus_generation_bump(&st->gen);
    return 0;
}

static int hello_stop(const struct canopus_context_v1 *ctx)
{
    struct hello_state *st = &s_state;
    (void)ctx;
    canopus_tracker_release_all(&st->tracker);
    return 0;
}

static int hello_query(struct canopus_status_writer_v1 *writer)
{
    struct hello_state *st = &s_state;
    if (writer != 0) {
        struct canopus_status_writer_v1 tmp = *writer;
        canopus_status_put_u32(&tmp, HELLO_MAGIC);
        canopus_status_put_u32(&tmp, HELLO_STATUS_VERSION);
        canopus_status_put_u32(&tmp, st->lc.state);
        canopus_status_put_u32(&tmp, st->timer_fires);
        canopus_status_writer_publish(&tmp);
        *writer = tmp;
    }
    return 0;
}

static const struct canopus_module_descriptor_v1 g_descriptor = {
    .struct_size = sizeof(struct canopus_module_descriptor_v1),
    .abi_major = CANOPUS_ABI_MAJOR,
    .abi_minor = CANOPUS_ABI_MINOR,
    .flags = 0u,
    .module_id = "org.example.hello",
    .module_version = "0.1.0",
    .build_id = "hello-0.1.0",
    .target_id = "xiaomi-band-10-pro-3.101.030",
    .prepare = hello_prepare,
    .activate = hello_activate,
    .deactivate = hello_deactivate,
    .stop = hello_stop,
    .query = hello_query,
};

const struct canopus_module_descriptor_v1 *hello_descriptor(void)
{
    return &g_descriptor;
}
