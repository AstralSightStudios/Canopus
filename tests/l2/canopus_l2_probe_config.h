#include <stdint.h>

void test_l2_rx(uint16_t, uint8_t, uint8_t, void *, uint16_t);
void *test_l2_session(uint16_t);
int test_l2_crypto(void *, int, uint16_t, const void *, void *);
int test_l2_send(void *, const void *);
void *test_l2_timer(void (*)(void *), uint32_t, void *);
extern uint32_t test_l2_head[2];
extern void *test_l2_loop;
extern void *test_l2_bt_loop;
int test_l2_owner_init(void *, void *);
int test_l2_owner_start(void *, void (*)(void *), uint64_t, uint64_t);
void test_l2_timer_delete(void *);
#define L2_PROBE_OWNER_LOOP ((uintptr_t)&test_l2_loop)
#define L2_PROBE_BT_LOOP ((uintptr_t)&test_l2_bt_loop)
#define L2_PROBE_OWNER_TIMER_INIT ((uintptr_t)test_l2_owner_init)
#define L2_PROBE_OWNER_TIMER_START ((uintptr_t)test_l2_owner_start)
#define L2_PROBE_OWNER_TIMER_SIZE 136u
#define L2_PROBE_TIMER_DELETE ((uintptr_t)test_l2_timer_delete)
#define L2_PROBE_RX ((uintptr_t)test_l2_rx)
#define L2_PROBE_SEND ((uintptr_t)test_l2_send)
#define L2_PROBE_SESSION ((uintptr_t)test_l2_session)
#define L2_PROBE_CRYPTO ((uintptr_t)test_l2_crypto)
#define L2_PROBE_TIMER ((uintptr_t)test_l2_timer)
#define L2_PROBE_CONNECTIONS ((uintptr_t)test_l2_head)
