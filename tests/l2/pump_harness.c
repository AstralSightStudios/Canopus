/* The owner pump without the L2 transport: background timers keep a source on the UI owner loop
 * (bootstrapped once from LVGL) on targets that have no SAR/session/crypto entry points. */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_l2_probe_config.h"
#include "canopus_services.h"

void *test_l2_loop = (void *)0x2000a000u;
static void (*bootstrap_tick)(void *), (*owner_tick)(void *);
static uint32_t timers_created, owner_inits, owner_starts, bootstrap_deleted, polls;
static void *owner_handle;
static int poll_data;

void *test_l2_timer(void (*cb)(void *), uint32_t period, void *data)
{
    (void)data;
    if (period != 10u) return 0;
    timers_created++;
    bootstrap_tick = cb;
    return (void *)0x20009000u;
}
int test_l2_owner_init(void *loop, void *timer)
{
    if (loop != test_l2_loop) return -1;
    owner_inits++;
    owner_handle = timer;
    return 0;
}
int test_l2_owner_start(void *timer, void (*cb)(void *), uint64_t timeout, uint64_t repeat)
{
    if (timer != owner_handle || timeout != 10u || repeat != 10u) return -1;
    owner_starts++;
    owner_tick = cb;
    return 0;
}
void test_l2_timer_delete(void *timer)
{
    if (timer == (void *)0x20009000u) bootstrap_deleted++;
}

static void background_poll(void *data)
{
    if (data == &poll_data) polls++;
}

int exercise(void)
{
    uint8_t frame[2] = {1, 2};
    if (canopus_sup_l2_background(background_poll, &poll_data) != 0) return 1;
    if (timers_created != 1u || owner_inits != 0u || bootstrap_tick == 0) return 2;
    /* No transport: nothing to send, no request ring to report. */
    if (sup_l2_send(0, 0x43u, frame, sizeof(frame)) != CANOPUS_SVC_ERR_UNSUPPORTED) return 3;
    if (sup_l2_request_slots(0) != 0u) return 4;
    /* The LVGL bootstrap runs once on the UI owner and hands over to the resident uv timer. */
    bootstrap_tick((void *)0x20009000u);
    if (owner_inits != 1u || owner_starts != 1u || bootstrap_deleted != 1u || owner_tick == 0) return 5;
    if (canopus_sup_l2_start() != 0 || timers_created != 1u) return 6;
    /* Screen off: only the owner timer fires, and every tick reaches the background poll. */
    owner_tick(owner_handle);
    owner_tick(owner_handle);
    if (polls != 2u) return 7;
    canopus_sup_l2_poll(); /* no transport to serve */
    if (polls != 2u) return 8;
    return 0;
}
