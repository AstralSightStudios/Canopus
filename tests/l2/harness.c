#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_l2_probe_config.h"
#include "canopus_memory.h"

uint32_t test_l2_head[2];
static uint32_t node[4], connection[4];
static uint32_t originals, dispatched, sent;
static int decrypt_error;
static uintptr_t session = 0x20008000u;
void *test_l2_loop = (void *)0x2000a000u;
void *test_l2_bt_loop = (void *)0x2000b000u;
static void (*bootstrap_tick)(void *), (*owner_tick)(void *), (*bt_tick)(void *);
static uint32_t owner_init_count, bootstrap_deleted;
static void *owner_handle, *bt_handle;

void test_l2_rx(uint16_t h, uint8_t c, uint8_t op, void *p, uint16_t n)
{
    (void)h; (void)c; (void)op; (void)p; (void)n;
    originals++;
}
void *test_l2_session(uint16_t h) { return h == 7u ? (void *)session : 0; }
int test_l2_crypto(void *s, int encrypt, uint16_t len, const void *in, void *out)
{
    (void)s; (void)encrypt;
    if (decrypt_error) return -1;
    canopus_memcpy(out, in, len);
    return 0;
}
int test_l2_send(void *s, const void *p)
{
    (void)s; (void)p;
    sent++;
    return 0;
}
void *test_l2_timer(void (*cb)(void *), uint32_t period, void *data)
{
    (void)period; (void)data;
    bootstrap_tick = cb;
    return (void *)0x20009000u;
}
int test_l2_owner_init(void *loop, void *timer)
{
    if (loop != test_l2_loop && loop != test_l2_bt_loop) return -1;
    owner_init_count++;
    if (loop == test_l2_loop) owner_handle = timer;
    else bt_handle = timer;
    return 0;
}
int test_l2_owner_start(void *timer, void (*cb)(void *), uint64_t timeout,
                        uint64_t repeat)
{
    if (timeout != 10u || repeat != 10u) return -1;
    if (timer == owner_handle) owner_tick = cb;
    else if (timer == bt_handle) bt_tick = cb;
    else return -1;
    return 0;
}
void test_l2_timer_delete(void *timer)
{
    if (timer == (void *)0x20009000u) bootstrap_deleted++;
}
struct canopus_supervisor_v1 *canopus_supervisor_get(void) { return 0; }
int32_t canopus_supervisor_handle_l2_packet(struct canopus_supervisor_v1 *sup,
    const uint8_t *in, uint32_t len, uint8_t *out, uint32_t cap)
{
    (void)sup; (void)cap;
    if (len != 36u || in[0] != 0x32) return -1;
    dispatched++;
    out[0] = 0x42;
    return 1;
}

int exercise(void)
{
    typedef void (*rx_fn)(uint16_t, uint8_t, uint8_t, void *, uint16_t);
    rx_fn rx;
    uint8_t request[36];
    canopus_memset(request, 0, sizeof(request)); request[0] = 0x32;
    test_l2_head[0] = test_l2_head[1] = (uint32_t)(uintptr_t)node;
    node[0] = node[1] = (uint32_t)(uintptr_t)test_l2_head;
    node[3] = (uint32_t)(uintptr_t)connection;
    connection[3] = L2_PROBE_RX;
    if (canopus_sup_l2_start() != 0 || owner_init_count != 0u) return 1;
    bootstrap_tick((void *)0x20009000u);
    if (connection[3] == L2_PROBE_RX || owner_init_count != 1u ||
        owner_tick == 0 || bootstrap_deleted != 1u) return 8;
    if (canopus_sup_l2_start() != 0 || owner_init_count != 1u) return 9;
    /* Screen-off stops LVGL: only the independent owner timer fires below. */
    rx = (rx_fn)(uintptr_t)connection[3];
    rx(7u, 1u, 2u, request, sizeof(request));
    if (originals != 1u) return 2;
    rx(7u, 0x43u, 1u, request, sizeof(request));
    owner_tick(owner_handle);
    if (dispatched != 0u) return 3;
    decrypt_error = 1;
    rx(7u, 0x43u, 2u, request, sizeof(request));
    owner_tick(owner_handle);
    if (dispatched != 0u) return 4;
    decrypt_error = 0;
    rx(7u, 0x43u, 2u, request, sizeof(request));
    if (dispatched != 0u) return 5;
    owner_tick(owner_handle);
    if (dispatched != 1u || sent != 0u) return 6;
    if (bt_tick == 0) return 60;
    bt_tick(bt_handle);
    if (sent != 1u) return 61;
    rx(7u, 0x43u, 2u, request, sizeof(request));
    session += 4u;
    owner_tick(owner_handle);
    if (bt_tick) bt_tick(bt_handle);
    if (dispatched != 1u || sent != 1u) return 7;
    return 0;
}
