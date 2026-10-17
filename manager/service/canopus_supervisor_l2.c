#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_memory.h"
#include "canopus_services.h"

#ifdef CANOPUS_SUP_EXPERIMENTAL_L2
#include "canopus_l2_probe_config.h"

#define L2_FRAME_MAX (CANOPUS_TRANSPORT_V2_HEADER_SIZE + CANOPUS_PROTO_MAX_PAYLOAD)
#define L2_QUEUE_SIZE 4u

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

static struct {
    volatile uint32_t state;
    uint16_t handle, len;
    void *session;
    uint8_t data[L2_FRAME_MAX];
} s_requests[L2_QUEUE_SIZE];
static uint32_t s_read, s_write;
static struct {
    volatile uint32_t state;
    uint16_t handle, len;
    void *session;
    uint32_t channel;
    uint8_t data[L2_FRAME_MAX];
} s_responses[L2_QUEUE_SIZE];
static uint32_t s_tx_read, s_tx_write;
static void *s_timer;
static uint64_t s_owner_timer[L2_PROBE_OWNER_TIMER_SIZE / sizeof(uint64_t)];
static uint64_t s_bt_timer[L2_PROBE_OWNER_TIMER_SIZE / sizeof(uint64_t)];
static int s_owner_initialized, s_owner_running;
static int s_bt_initialized, s_bt_running;
static uint16_t s_last_handle;
static void *s_last_session;
static int s_connected;

static int l2_bt_start(void);

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
    if (channel != CANOPUS_L2_CHANNEL) {
        ((l2_rx_fn)(uintptr_t)L2_PROBE_RX)(handle, channel, opcode, payload, len);
        return;
    }
    /* Control mutations require the paired session's encrypted transport. */
    if (opcode != CANOPUS_L2_OPCODE_WRITE_ENC || len > L2_FRAME_MAX ||
        len < CANOPUS_TRANSPORT_V2_HEADER_SIZE) return;
    session = ((session_fn)(uintptr_t)L2_PROBE_SESSION)(handle);
    if (session == 0 || l2_bt_start() != 0) return;
    mask = l2_lock();
    slot = s_write;
    if (s_requests[slot].state != 0u) { l2_unlock(mask); return; }
    s_requests[slot].state = 1u;
    s_write = (slot + 1u) % L2_QUEUE_SIZE;
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
    __asm__ volatile("dmb" ::: "memory");
    s_requests[slot].state = 2u;
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

static int l2_send(uint16_t handle, uint8_t channel, void *payload, uint16_t len)
{
    typedef int (*send_fn)(void *, const struct l2_packet *);
    struct l2_packet packet;
    void *session = ((session_fn)(uintptr_t)L2_PROBE_SESSION)(handle);
    if (session == 0) return CANOPUS_SVC_ERR_STATE;
    canopus_memset(&packet, 0, sizeof(packet));
    packet.channel = channel;
    packet.opcode = CANOPUS_L2_OPCODE_WRITE_ENC;
    packet.payload = payload;
    packet.len = len;
    /* The recovered SAR submit copies the encrypted bytes before returning. */
    return ((send_fn)(uintptr_t)L2_PROBE_SEND)(session, &packet);
}

static int l2_queue_send(uint16_t handle, void *session, uint8_t channel,
                         const void *payload, uint16_t len)
{
    uint32_t mask = l2_lock(), slot = s_tx_write;
    if (s_responses[slot].state != 0u) {
        l2_unlock(mask);
        return CANOPUS_SVC_ERR_NOMEM;
    }
    s_responses[slot].state = 1u;
    s_tx_write = (slot + 1u) % L2_QUEUE_SIZE;
    l2_unlock(mask);
    s_responses[slot].handle = handle;
    s_responses[slot].session = session;
    s_responses[slot].channel = channel;
    s_responses[slot].len = len;
    canopus_memcpy(s_responses[slot].data, payload, len);
    __asm__ volatile("dmb" ::: "memory");
    s_responses[slot].state = 2u;
    return 0;
}

static void l2_bt_tick(void *timer)
{
    uint32_t slot = s_tx_read;
    (void)timer;
    if (s_responses[slot].state != 2u) return;
    __asm__ volatile("dmb" ::: "memory");
    if (((session_fn)(uintptr_t)L2_PROBE_SESSION)(s_responses[slot].handle) ==
            s_responses[slot].session)
        (void)l2_send(s_responses[slot].handle, (uint8_t)s_responses[slot].channel,
                      s_responses[slot].data, s_responses[slot].len);
    __asm__ volatile("dmb" ::: "memory");
    s_responses[slot].state = 0u;
    s_tx_read = (slot + 1u) % L2_QUEUE_SIZE;
}

static int l2_native_timer_start(uintptr_t loop_addr, void *timer,
                                  int *initialized, void (*tick)(void *))
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
    return ((start_fn)(uintptr_t)L2_PROBE_OWNER_TIMER_START)(timer, tick, 10u, 10u);
}

static int l2_bt_start(void)
{
    int err;
    if (s_bt_running) return 0;
    /* RX already runs on the transport owner. Its SPP pipe fd and libuv
     * queues belong to this task, not the UI task that dispatches commands. */
    err = l2_native_timer_start(L2_PROBE_BT_LOOP, s_bt_timer,
                                &s_bt_initialized, l2_bt_tick);
    if (err == 0) s_bt_running = 1;
    return err;
}

void canopus_sup_l2_poll(void)
{
    uint32_t slot = s_read;
    int32_t len;
    l2_attach();
    if (s_requests[slot].state != 2u) return;
    __asm__ volatile("dmb" ::: "memory");
    if (s_requests[slot].len != 0u &&
        ((session_fn)(uintptr_t)L2_PROBE_SESSION)(s_requests[slot].handle) ==
            s_requests[slot].session) {
        s_last_handle = s_requests[slot].handle;
        s_last_session = s_requests[slot].session;
        s_connected = 1;
        len = canopus_supervisor_handle_l2_packet(canopus_supervisor_get(),
            s_requests[slot].data, s_requests[slot].len,
            s_requests[slot].data, L2_FRAME_MAX);
        if (len > 0) (void)l2_queue_send(s_last_handle, s_last_session,
            CANOPUS_L2_CHANNEL, s_requests[slot].data, (uint16_t)len);
    }
    __asm__ volatile("dmb" ::: "memory");
    s_requests[slot].state = 0u;
    s_read = (slot + 1u) % L2_QUEUE_SIZE;
}

static void l2_owner_tick(void *timer)
{
    (void)timer;
    canopus_sup_l2_poll();
}

static void l2_tick(void *timer)
{
    typedef void (*delete_fn)(void *);

    /* Bootstrap on the UI owner, never mutate its libuv heap from NSH/BT.
     * The vendor stops miwear-refresh (and thus LVGL) on screen-off; this
     * separate resident libuv timer stays on the same owner loop. */
    if (l2_native_timer_start(L2_PROBE_OWNER_LOOP, s_owner_timer,
                              &s_owner_initialized, l2_owner_tick) != 0) return;
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

int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len)
{
    (void)cookie;
    if (!s_connected) return CANOPUS_SVC_ERR_STATE;
    if (payload == 0 || len == 0u || len > L2_FRAME_MAX) return CANOPUS_SVC_ERR_IO;
    return l2_queue_send(s_last_handle, s_last_session, channel, payload, len);
}

#else
int canopus_sup_l2_start(void) { return CANOPUS_SVC_ERR_UNSUPPORTED; }
void canopus_sup_l2_poll(void) {}
int sup_l2_send(void *cookie, uint8_t channel, const void *payload, uint16_t len)
{
    (void)cookie; (void)channel; (void)payload; (void)len;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
}
#endif
