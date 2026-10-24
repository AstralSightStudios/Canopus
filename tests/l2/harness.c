#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_l2_probe_config.h"
#include "canopus_memory.h"

uint32_t test_l2_head[2];
static uint32_t node[4], connection[4];
static uint32_t originals, dispatched, sent;
static uint8_t sent_head[64][2]; /* first two plaintext payload bytes of each frame accepted */
static uint32_t send_attempts;
static int send_failure;
static uint8_t attempted_head[2];
static int decrypt_error;
static uintptr_t session = 0x20008000u;
void *test_l2_loop = (void *)0x2000a000u;
void *test_l2_bt_loop = (void *)0x2000b000u;
static void (*bootstrap_tick)(void *), (*owner_tick)(void *), (*bt_tick)(void *);
static uint32_t owner_init_count, bootstrap_deleted, bt_period, bt_starts;
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
    /* struct l2_packet: channel, opcode, reserved, then the payload pointer. */
    uint8_t *payload = *(uint8_t *const *)((const uint8_t *)p + 4);
    uint16_t len = *(const uint16_t *)((const uint8_t *)p + 8);
    (void)s;
    send_attempts++;
    attempted_head[0] = payload[0];
    attempted_head[1] = len > 1u ? payload[1] : 0;
    /* Native transport copies ciphertext back before SAR can reject the send.
     * Queue storage must therefore remain plaintext even after a failed attempt. */
    for (uint16_t i = 0; i < len; i++) payload[i] ^= 0xa5u;
    if (send_failure) return -1;
    if (sent < 64u) {
        sent_head[sent][0] = attempted_head[0];
        sent_head[sent][1] = attempted_head[1];
    }
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
    if (timer == owner_handle) {
        if (timeout != 10u || repeat != 10u) return -1;
        owner_tick = cb;
    } else if (timer == bt_handle) {
        if (timeout != repeat || (repeat != 2u && repeat != 10u)) return -1;
        bt_period = (uint32_t)repeat;
        bt_starts++;
        bt_tick = cb;
    } else return -1;
    return 0;
}
void test_l2_timer_delete(void *timer)
{
    if (timer == (void *)0x20009000u) bootstrap_deleted++;
}
/* The deep request ring comes from the platform heap. */
static uint32_t heap_buffer[16u * 160u];
static uint32_t heap_allocs;
static void *harness_heap_alloc(void *cookie, uint32_t size)
{
    (void)cookie;
    heap_allocs++;
    return size <= sizeof(heap_buffer) ? (void *)heap_buffer : 0;
}
static struct canopus_sup_platform_v1 harness_platform;
static struct canopus_supervisor_v1 harness_sup;
struct canopus_supervisor_v1 *canopus_supervisor_get(void)
{
    harness_platform.heap_alloc = harness_heap_alloc;
    harness_sup.platform = &harness_platform;
    return &harness_sup;
}
int canopus_supervisor_l2_rx_registered(uint8_t channel) { (void)channel; return 0; }
int canopus_supervisor_l2_rx_dispatch(struct canopus_supervisor_v1 *sup, uint8_t channel,
                                      const void *payload, uint16_t len)
{
    (void)sup; (void)channel; (void)payload; (void)len;
    return 0;
}
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
    if (sup_l2_request_slots(0) != 0u) return 10; /* no ring before the owner's first poll */
    bootstrap_tick((void *)0x20009000u);
    if (connection[3] == L2_PROBE_RX || owner_init_count != 1u ||
        owner_tick == 0 || bootstrap_deleted != 1u) return 8;
    if (sup_l2_request_slots(0) != 16u || heap_allocs != 1u) return 11;
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
    if (dispatched != 0u || bt_period != 2u || bt_tick == 0) return 5;
    bt_tick(bt_handle);
    if (bt_period != 2u || sent != 0u) return 62;
    owner_tick(owner_handle);
    if (dispatched != 1u || sent != 0u) return 6;
    bt_tick(bt_handle);
    if (sent != 1u || bt_period != 10u) return 61;
    {
        uint32_t starts = bt_starts;
        bt_tick(bt_handle);
        if (bt_starts != starts) return 63;
    }
    rx(7u, 0x43u, 2u, request, sizeof(request));
    if (bt_period != 2u || owner_init_count != 2u) return 64;
    session += 4u;
    owner_tick(owner_handle);
    if (bt_tick) bt_tick(bt_handle);
    if (dispatched != 1u || sent != 1u) return 7;

    /* CanopusConnect: the peer epoch follows the phone session, and events leave
     * on their own queue, after Manager responses. */
    {
        uint8_t frame[2] = {0xe7, 1};
        uint32_t epoch_old = sup_l2_peer(0), epoch_new, first = sent;
        if (epoch_old == 0u) return 70;
        rx(7u, 0x43u, 2u, request, sizeof(request)); /* the new session's first request */
        owner_tick(owner_handle);
        epoch_new = sup_l2_peer(0);
        if (dispatched != 2u || epoch_new == epoch_old) return 71;
        rx(7u, 0x43u, 2u, request, sizeof(request)); /* same session again */
        owner_tick(owner_handle);
        if (dispatched != 3u || sup_l2_peer(0) != epoch_new) return 72;
        if (sup_l2_event(0, frame, 2u) != 0) return 73;
        frame[1] = 2;
        if (sup_l2_event(0, frame, 2u) != 0) return 74;
        frame[1] = 3;
        if (sup_l2_event(0, frame, 2u) == 0) return 75; /* two event slots, both taken */
        if (sup_l2_event(0, frame, 0u) == 0 || sup_l2_event(0, 0, 2u) == 0) return 76;
        /* Two responses and two events wait: a response goes first, then they take turns, each
         * queue in its own order, so an event never waits for the responses to run out. */
        for (int i = 0; i < 4; i++) bt_tick(bt_handle);
        if (sent != first + 4u || sent_head[first][0] != 0x42 ||
            sent_head[first + 1u][0] != 0xe7 || sent_head[first + 1u][1] != 1 ||
            sent_head[first + 2u][0] != 0x42 ||
            sent_head[first + 3u][0] != 0xe7 || sent_head[first + 3u][1] != 2) return 77;
        bt_tick(bt_handle);
        if (sent != first + 4u) return 78;
        /* An event queued for a session that is gone is dropped, not sent. */
        frame[1] = 4;
        if (sup_l2_event(0, frame, 2u) != 0) return 79;
        session += 4u;
        bt_tick(bt_handle);
        if (sent != first + 4u) return 80;
        if (sup_l2_event(0, frame, 2u) != 0) return 81; /* the slot was freed */
        bt_tick(bt_handle);                              /* drop that event's turn */
        bt_tick(bt_handle);
    }

    /* A burst: the ring's four requests are all served by one owner tick, in order, and the
     * answers wait on the response queue. A fifth request is not served while that queue is full
     * (its answer would be lost); it is served after the Bluetooth timer made room. */
    {
        uint32_t before = dispatched, first = sent;
        int i;
        for (i = 0; i < 4; i++) {
            request[0] = 0x32;
            rx(7u, 0x43u, 2u, request, sizeof(request));
        }
        owner_tick(owner_handle);
        if (dispatched != before + 4u) return 90;
        rx(7u, 0x43u, 2u, request, sizeof(request));
        owner_tick(owner_handle);
        if (dispatched != before + 4u) return 91; /* waits: no room for its answer */
        bt_tick(bt_handle);
        if (sent != first + 1u) return 92;
        owner_tick(owner_handle);
        if (dispatched != before + 5u) return 93;
        for (i = 0; i < 5; i++) bt_tick(bt_handle);
        if (sent != first + 5u) return 94;
    }

    /* The deep ring: the phone may keep sixteen requests outstanding. A seventeenth finds no
     * slot and is dropped (the phone never sends one: the depth is what QUERY_DEVICE reports). */
    {
        uint32_t before = dispatched, first = sent;
        int i;
        for (i = 0; i < 17; i++) {
            request[0] = 0x32;
            rx(7u, 0x43u, 2u, request, sizeof(request));
        }
        for (i = 0; i < 40; i++) {
            owner_tick(owner_handle);
            bt_tick(bt_handle);
        }
        if (dispatched != before + 16u || sent != first + 16u) return 95;
        if (bt_period != 10u) return 96;
    }
    /* A failed event remains at the head with its original bytes; retries each
     * consume only one BT tick, preserve order, and keep the two slots occupied. */
    {
        uint8_t frame[2] = {0xe7, 0x31};
        uint32_t first = sent, attempts = send_attempts;
        if (sup_l2_event(0, frame, 2u) != 0) return 100;
        frame[1]++;
        if (sup_l2_event(0, frame, 2u) != 0) return 101;
        send_failure = 1;
        for (int i = 0; i < 3; i++) {
            bt_tick(bt_handle);
            if (send_attempts != attempts + (uint32_t)i + 1u || sent != first ||
                attempted_head[0] != 0xe7 || attempted_head[1] != 0x31 ||
                bt_period != 2u) return 102;
        }
        if (sup_l2_event(0, frame, 2u) != CANOPUS_SVC_ERR_BUSY) return 103;
        send_failure = 0;
        bt_tick(bt_handle);
        if (sent != first + 1u || sent_head[first][0] != 0xe7 ||
            sent_head[first][1] != 0x31) return 104;
        bt_tick(bt_handle);
        if (sent != first + 2u || sent_head[first + 1u][1] != 0x32 ||
            bt_period != 10u) return 105;
        bt_tick(bt_handle);
        if (sent != first + 2u) return 106;
    }

    /* Failed RPC responses exert backpressure without re-executing callbacks.
     * A queued event still gets its usual turn between response attempts. */
    {
        uint8_t frame[2] = {0xe7, 0x41};
        uint32_t first = sent, before = dispatched, attempts = send_attempts;
        for (int i = 0; i < 5; i++) rx(7u, 0x43u, 2u, request, sizeof(request));
        owner_tick(owner_handle);
        if (dispatched != before + 4u) return 110;
        if (sup_l2_event(0, frame, 2u) != 0) return 111;
        send_failure = 1;
        bt_tick(bt_handle);
        if (send_attempts != attempts + 1u || sent != first || attempted_head[0] != 0x42) return 112;
        owner_tick(owner_handle);
        if (dispatched != before + 4u) return 113;
        bt_tick(bt_handle);
        if (send_attempts != attempts + 2u || attempted_head[0] != 0xe7 ||
            attempted_head[1] != 0x41) return 114;
        send_failure = 0;
        bt_tick(bt_handle);
        if (sent != first + 1u || sent_head[first][0] != 0x42) return 115;
        owner_tick(owner_handle);
        if (dispatched != before + 5u) return 116;
        bt_tick(bt_handle);
        if (sent != first + 2u || sent_head[first + 1u][0] != 0xe7 ||
            sent_head[first + 1u][1] != 0x41) return 117;
        for (int i = 0; i < 4; i++) bt_tick(bt_handle);
        if (sent != first + 6u || bt_period != 10u) return 118;
    }

    /* Both queues discard a failed old-session frame after disconnect or
     * replacement, without invoking native send again or wedging the head. */
    for (int replaced = 0; replaced < 2; replaced++) {
        uint8_t frame[2] = {0xe7, 0x51};
        uintptr_t previous = session;
        uint32_t first = sent, attempts;
        rx(7u, 0x43u, 2u, request, sizeof(request));
        owner_tick(owner_handle);
        if (sup_l2_event(0, frame, 2u) != 0) return 120;
        send_failure = 1;
        bt_tick(bt_handle);
        bt_tick(bt_handle);
        attempts = send_attempts;
        session = replaced ? previous + 4u : 0;
        send_failure = 0;
        for (int i = 0; i < 4; i++) bt_tick(bt_handle);
        if (sent != first || send_attempts != attempts || bt_period != 10u) return 121;
        if (!replaced) session = previous + 4u;
        rx(7u, 0x43u, 2u, request, sizeof(request));
        owner_tick(owner_handle);
        if (sup_l2_event(0, frame, 2u) != 0) return 122;
        for (int i = 0; i < 4; i++) bt_tick(bt_handle);
        if (sent != first + 2u || bt_period != 10u) return 123;
    }
    return 0;
}
