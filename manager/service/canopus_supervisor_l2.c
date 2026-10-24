#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "canopus_services.h"

#include "canopus_l2_probe_config.h"

#ifndef CANOPUS_SUP_HAS_OWNER_PUMP
#define CANOPUS_SUP_HAS_OWNER_PUMP CANOPUS_SUP_HAS_L2
#endif

/* Two layers. The owner pump -- a resident libuv timer on the UI owner's loop, bootstrapped once
 * from LVGL -- keeps ticking with the screen off and drives the background timers; it needs only
 * that loop and the uv_timer entry points. The encrypted L2 transport is served from the same
 * timer and needs the SAR, session and crypto entry points as well, which not every target has. */
#if CANOPUS_SUP_HAS_OWNER_PUMP

static void *s_timer;
static uint64_t s_owner_timer[L2_PROBE_OWNER_TIMER_SIZE / sizeof(uint64_t)];
static int s_owner_initialized, s_owner_running;
static void (*s_background_poll)(void *);
static void *s_background_data;

static int l2_native_timer_start(uintptr_t loop_addr, void *timer,
                                  int *initialized, void (*tick)(void *), uint32_t period)
{
    typedef int (*init_fn)(void *, void *);
    typedef int (*start_fn)(void *, void (*)(void *), uint64_t, uint64_t);
    void *loop = *(void *volatile *)loop_addr;
    int err;
    if (!*initialized) {
        err = ((init_fn)(uintptr_t)L2_PROBE_OWNER_TIMER_INIT)(loop, timer);
        if (err != 0) return err;
        *initialized = 1;
    }
    return ((start_fn)(uintptr_t)L2_PROBE_OWNER_TIMER_START)(timer, tick, period, period);
}

#if CANOPUS_SUP_HAS_L2

#define L2_FRAME_MAX (CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_PROTO_MAX_PAYLOAD)
/* Requests the phone may have outstanding. Every one of them waits a round trip of the
 * Bluetooth link for its answer, so this, not the link, set the import speed: four slots (three
 * for CanopusConnect) are 1.4 KiB per round trip. The ring of this depth lives on the heap,
 * allocated on the owner's first poll (the resident image has no room for it); until then, or if
 * that allocation fails, a static ring of four serves. QUERY_DEVICE reports the depth in use. */
#define L2_RX_SLOTS 16u
#define L2_RX_FALLBACK_SLOTS 4u
#define L2_QUEUE_SIZE 4u
#define L2_EVENT_QUEUE_SIZE 2u
/* Requests one owner tick may serve: as many as the response queue can take. A phone that keeps
 * several CanopusConnect messages in flight has them answered in one 10 ms tick instead of one
 * per tick, so the tick period stops being the throughput ceiling. */
#define L2_POLL_BATCH L2_QUEUE_SIZE

typedef void (*l2_rx_fn)(uint16_t, uint8_t, uint8_t, void *, uint16_t);
typedef void *(*session_fn)(uint16_t);
typedef int (*crypto_fn)(void *, int, uint16_t, const void *, void *);

struct l2_packet {
    uint8_t channel, opcode;
    uint16_t reserved;
    void *payload;
    uint16_t len, padding;
    void *complete, *complete_cookie, *sent, *sent_cookie;
};
_Static_assert(sizeof(struct l2_packet) == 28u, "MiWear packet ABI");

struct l2_request {
    volatile uint32_t state;
    uint16_t handle, len;
    uint8_t channel;
    void *session;
    uint8_t data[L2_FRAME_MAX];
};
static struct l2_request s_request_fallback[L2_RX_FALLBACK_SLOTS];
/* The ring in use and its length; both change once, under l2_lock, before RX is hooked. */
static struct l2_request *s_requests;
static uint32_t s_request_slots;
static uint32_t s_read, s_write;
struct l2_tx {
    volatile uint32_t state;
    uint16_t handle, len;
    void *session;
    uint32_t channel;
    uint8_t data[L2_FRAME_MAX];
};
/* Manager responses and CanopusConnect events never share a queue, so a chatty module cannot
 * fill the response queue; the Bluetooth owner gives each queue its turn. */
static struct l2_tx s_responses[L2_QUEUE_SIZE];
static uint32_t s_tx_read, s_tx_write;
static struct l2_tx s_events[L2_EVENT_QUEUE_SIZE];
static uint32_t s_ev_read, s_ev_write;
/* A response just left while an event waited: the event goes next (see l2_bt_tick). */
static int s_event_turn;
static uint32_t s_peer_epoch;
static uint64_t s_bt_timer[L2_PROBE_OWNER_TIMER_SIZE / sizeof(uint64_t)];
static int s_bt_initialized;
static uint32_t s_bt_period;
static uint16_t s_last_handle;
static void *s_last_session;
static int s_connected;

static int l2_bt_start(uint32_t period);

static uint32_t l2_lock(void)
{
    uint32_t mask;
    __asm__ volatile("mrs %0, primask\n\tcpsid i" : "=r"(mask) :: "memory");
    return mask;
}

static void l2_unlock(uint32_t mask)
{
    __asm__ volatile("msr primask, %0" :: "r"(mask) : "memory");
}

static void l2_rx(uint16_t handle, uint8_t channel, uint8_t opcode,
                   void *payload, uint16_t len)
{
    uint32_t slot, mask;
    void *session;
    /* A channel a module registered through ctx->bt->l2_register_rx is
     * consumed here, decrypted the same way as the control channel; every
     * other channel keeps going to the stock handler. */
    if (channel != CANOPUS_L2_CHANNEL && !canopus_supervisor_l2_rx_registered(channel)) {
        ((l2_rx_fn)(uintptr_t)L2_PROBE_RX)(handle, channel, opcode, payload, len);
        return;
    }
    /* Control mutations require the paired session's encrypted transport. */
    if (opcode != CANOPUS_L2_OPCODE_WRITE_ENC || len > L2_FRAME_MAX || len == 0u ||
        (channel == CANOPUS_L2_CHANNEL && len < CANOPUS_TRANSPORT_V2_HEADER_SIZE)) return;
    session = ((session_fn)(uintptr_t)L2_PROBE_SESSION)(handle);
    if (session == 0 || l2_bt_start(2u) != 0) return;
    mask = l2_lock();
    slot = s_write;
    if (s_requests == 0 || s_requests[slot].state != 0u) { l2_unlock(mask); return; }
    s_requests[slot].state = 1u;
    s_write = (slot + 1u) % s_request_slots;
    l2_unlock(mask);
    /* Decode into owned storage; failed decryption must never dispatch a command. */
    if (((crypto_fn)(uintptr_t)L2_PROBE_CRYPTO)(session, 0, len, payload,
                                               s_requests[slot].data) != 0) {
        s_requests[slot].len = 0u;
    } else {
        s_requests[slot].len = len;
    }
    s_requests[slot].session = session;
    s_requests[slot].handle = handle;
    s_requests[slot].channel = channel;
    __asm__ volatile("dmb" ::: "memory");
    s_requests[slot].state = 2u;
}

/* Sets up the request ring: the deep one on the heap when the platform can spare it. Runs on the
 * owner before the first l2_attach, so no RX is hooked yet and the ring is still empty. */
static void l2_ring_init(void)
{
    struct canopus_supervisor_v1 *sup;
    struct l2_request *ring = 0;
    uint32_t slots = L2_RX_FALLBACK_SLOTS, mask;
    if (s_requests != 0) return;
    sup = canopus_supervisor_get();
    if (sup != 0 && sup->platform != 0 && sup->platform->heap_alloc != 0) {
        ring = (struct l2_request *)sup->platform->heap_alloc(sup->platform_cookie,
            (uint32_t)(L2_RX_SLOTS * sizeof(struct l2_request)));
    }
    if (ring != 0) {
        canopus_memset(ring, 0, L2_RX_SLOTS * sizeof(struct l2_request));
        slots = L2_RX_SLOTS;
    } else {
        ring = s_request_fallback;
    }
    mask = l2_lock();
    s_read = s_write = 0u;
    s_request_slots = slots;
    s_requests = ring;
    l2_unlock(mask);
}

static void l2_attach(void)
{
    volatile uint32_t *head = (volatile uint32_t *)(uintptr_t)L2_PROBE_CONNECTIONS;
    volatile uint32_t *node;
    uint32_t mask = l2_lock();
    /* SAR owns both the list nodes and connection objects; never retain them. */
    for (node = (volatile uint32_t *)(uintptr_t)head[1];
         node != 0 && node != head;
         node = (volatile uint32_t *)(uintptr_t)node[1]) {
        volatile uint32_t *conn = (volatile uint32_t *)(uintptr_t)node[3];
        if (conn[3] == L2_PROBE_RX) conn[3] = (uint32_t)(uintptr_t)l2_rx;
    }
    l2_unlock(mask);
}

static int l2_send(uint16_t handle, uint8_t channel, const void *payload, uint16_t len)
{
    typedef int (*send_fn)(void *, const struct l2_packet *);
    struct l2_packet packet;
    uint8_t scratch[L2_FRAME_MAX];
    void *session = ((session_fn)(uintptr_t)L2_PROBE_SESSION)(handle);
    if (session == 0) return CANOPUS_SVC_ERR_STATE;
    canopus_memset(&packet, 0, sizeof(packet));
    packet.channel = channel;
    packet.opcode = CANOPUS_L2_OPCODE_WRITE_ENC;
    /* Native send overwrites its payload with ciphertext before SAR submit
     * can reject it. Keep the queued plaintext intact for a later attempt. */
    canopus_memcpy(scratch, payload, len);
    packet.payload = scratch;
    packet.len = len;
    /* SAR submit synchronously copies accepted bytes; 0 means consumed,
     * nonzero means not accepted (see ADR-CAN-008, send/retry contract). */
    return ((send_fn)(uintptr_t)L2_PROBE_SEND)(session, &packet);
}

static int l2_enqueue(struct l2_tx *queue, uint32_t size, uint32_t *write, uint16_t handle,
                      void *session, uint8_t channel, const void *payload, uint16_t len)
{
    uint32_t mask = l2_lock(), slot = *write;
    if (queue[slot].state != 0u) {
        l2_unlock(mask);
        return -1;
    }
    queue[slot].state = 1u;
    *write = (slot + 1u) % size;
    l2_unlock(mask);
    queue[slot].handle = handle;
    queue[slot].session = session;
    queue[slot].channel = channel;
    queue[slot].len = len;
    canopus_memcpy(queue[slot].data, payload, len);
    __asm__ volatile("dmb" ::: "memory");
    queue[slot].state = 2u;
    return 0;
}

static int l2_queue_send(uint16_t handle, void *session, uint8_t channel,
                         const void *payload, uint16_t len)
{
    return l2_enqueue(s_responses, L2_QUEUE_SIZE, &s_tx_write, handle, session, channel,
                      payload, len) == 0 ? 0 : CANOPUS_SVC_ERR_NOMEM;
}

/* Attempts the oldest frame. A failed send still consumes this tick's turn;
 * keep its slot and read index until accepted or the recorded session is gone. */
static int l2_drain(struct l2_tx *queue, uint32_t size, uint32_t *read)
{
    uint32_t slot = *read;
    if (queue[slot].state != 2u) return 0;
    __asm__ volatile("dmb" ::: "memory");
    if (((session_fn)(uintptr_t)L2_PROBE_SESSION)(queue[slot].handle) == queue[slot].session) {
        if (l2_send(queue[slot].handle, (uint8_t)queue[slot].channel, queue[slot].data,
                    queue[slot].len) != 0) return 1;
    }
    __asm__ volatile("dmb" ::: "memory");
    queue[slot].state = 0u;
    *read = (slot + 1u) % size;
    return 1;
}

static void l2_bt_tick(void *timer)
{
    uint32_t period;
    (void)timer;
    /* Responses go first, but an event that waited behind one gets the next turn. With many
     * requests in flight a response is nearly always queued, and a module's events -- its flow
     * control among them -- would otherwise only leave in a lull. A Manager response waits at
     * most one tick for an event. */
    if (s_event_turn && l2_drain(s_events, L2_EVENT_QUEUE_SIZE, &s_ev_read)) {
        s_event_turn = 0;
    } else if (l2_drain(s_responses, L2_QUEUE_SIZE, &s_tx_read)) {
        s_event_turn = s_events[s_ev_read].state == 2u;
    } else {
        (void)l2_drain(s_events, L2_EVENT_QUEUE_SIZE, &s_ev_read);
        s_event_turn = 0;
    }
    period = (s_requests != 0 && s_requests[s_read].state != 0u) ||
             s_responses[s_tx_read].state != 0u ||
             s_events[s_ev_read].state != 0u ? 2u : 10u;
    (void)l2_bt_start(period);
}

static int l2_bt_start(uint32_t period)
{
    int err;
    if (s_bt_period == period) return 0;
    /* RX already runs on the transport owner. Its SPP pipe fd and libuv
     * queues belong to this task, not the UI task that dispatches commands. */
    err = l2_native_timer_start(L2_PROBE_BT_LOOP, s_bt_timer,
                                &s_bt_initialized, l2_bt_tick, period);
    if (err == 0) s_bt_period = period;
    return err;
}

/* Serves the oldest queued request. Returns 0 when there is nothing to serve, or when the
 * response queue is full: the request then stays queued for a later tick instead of losing its
 * answer, and the Bluetooth timer's draining makes room. */
static int l2_serve_one(void)
{
    uint32_t slot = s_read;
    int32_t len;
    if (s_requests == 0 || s_requests[slot].state != 2u) return 0;
    if (s_responses[s_tx_write].state != 0u) return 0;
    __asm__ volatile("dmb" ::: "memory");
    if (s_requests[slot].len != 0u &&
        ((session_fn)(uintptr_t)L2_PROBE_SESSION)(s_requests[slot].handle) ==
            s_requests[slot].session) {
        if (!s_connected || s_requests[slot].handle != s_last_handle ||
            s_requests[slot].session != s_last_session) {
            s_peer_epoch++;
        }
        s_last_handle = s_requests[slot].handle;
        s_last_session = s_requests[slot].session;
        s_connected = 1;
        if (s_requests[slot].channel != CANOPUS_L2_CHANNEL) {
            (void)canopus_supervisor_l2_rx_dispatch(canopus_supervisor_get(),
                s_requests[slot].channel, s_requests[slot].data, s_requests[slot].len);
        } else {
            len = canopus_supervisor_handle_l2_packet(canopus_supervisor_get(),
                s_requests[slot].data, s_requests[slot].len,
                s_requests[slot].data, L2_FRAME_MAX);
            if (len > 0) (void)l2_queue_send(s_last_handle, s_last_session,
                CANOPUS_L2_CHANNEL, s_requests[slot].data, (uint16_t)len);
        }
    }
    __asm__ volatile("dmb" ::: "memory");
    s_requests[slot].state = 0u;
    s_read = (slot + 1u) % s_request_slots;
    return 1;
}

void canopus_sup_l2_poll(void)
{
    uint32_t served;
    l2_ring_init();
    l2_attach();
    for (served = 0u; served < L2_POLL_BATCH; served++) {
        if (!l2_serve_one()) break;
    }
}

#else
void canopus_sup_l2_poll(void) {}
#endif /* CANOPUS_SUP_HAS_L2 */

static void l2_owner_tick(void *timer)
{
    (void)timer;
    canopus_sup_l2_poll();
    if (s_background_poll != 0) s_background_poll(s_background_data);
}

int canopus_sup_l2_background(void (*poll)(void *), void *data)
{
    s_background_data = data;
    s_background_poll = poll;
    return canopus_sup_l2_start();
}

static void l2_tick(void *timer)
{
    typedef void (*delete_fn)(void *);

    /* Bootstrap on the UI owner, never mutate its libuv heap from NSH/BT.
     * The vendor stops miwear-refresh (and thus LVGL) on screen-off; this
     * separate resident libuv timer stays on the same owner loop. */
    if (l2_native_timer_start(L2_PROBE_OWNER_LOOP, s_owner_timer,
                              &s_owner_initialized, l2_owner_tick, 10u) != 0) return;
    s_owner_running = 1;
    ((delete_fn)(uintptr_t)L2_PROBE_TIMER_DELETE)(timer);
    s_timer = 0;
    canopus_sup_l2_poll();
}

int canopus_sup_l2_start(void)
{
    typedef void *(*timer_fn)(void (*)(void *), uint32_t, void *);
    if (s_owner_running) return 0;
    if (s_timer == 0)
        s_timer = ((timer_fn)(uintptr_t)L2_PROBE_TIMER)(l2_tick, 10u, 0);
    return s_timer != 0 ? 0 : CANOPUS_SVC_ERR_NOMEM;
}

#if CANOPUS_SUP_HAS_L2
int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len)
{
    (void)cookie;
    if (!s_connected) return CANOPUS_SVC_ERR_STATE;
    if (payload == 0 || len == 0u || len > L2_FRAME_MAX) return CANOPUS_SVC_ERR_IO;
    return l2_queue_send(s_last_handle, s_last_session, channel, payload, len);
}

int sup_l2_event(void *cookie, const void *frame, uint16_t len)
{
    (void)cookie;
    if (!s_connected) return CANOPUS_SVC_ERR_STATE;
    if (frame == 0 || len == 0u || len > L2_FRAME_MAX) return CANOPUS_SVC_ERR_IO;
    /* The bluetooth timer is started by the first encrypted request, which also
     * makes s_connected true, so it is already running here. */
    return l2_enqueue(s_events, L2_EVENT_QUEUE_SIZE, &s_ev_write, s_last_handle, s_last_session,
                      CANOPUS_L2_CHANNEL, frame, len) == 0 ? 0 : CANOPUS_SVC_ERR_BUSY;
}

uint32_t sup_l2_peer(void *cookie)
{
    (void)cookie;
    return s_connected ? s_peer_epoch : 0u;
}

uint32_t sup_l2_request_slots(void *cookie)
{
    (void)cookie;
    return s_requests != 0 ? s_request_slots : 0u;
}
#else
int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len)
{
    (void)cookie; (void)channel; (void)payload; (void)len;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}
uint32_t sup_l2_request_slots(void *cookie)
{
    (void)cookie;
    return 0u;
}
#endif /* CANOPUS_SUP_HAS_L2 */

#else
int canopus_sup_l2_start(void) { return CANOPUS_SVC_ERR_UNSUPPORTED; }
void canopus_sup_l2_poll(void) {}
int canopus_sup_l2_background(void (*poll)(void *), void *data)
{
    (void)poll; (void)data;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}
int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len)
{
    (void)cookie; (void)channel; (void)payload; (void)len;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}
uint32_t sup_l2_request_slots(void *cookie)
{
    (void)cookie;
    return 0u;
}
#endif
