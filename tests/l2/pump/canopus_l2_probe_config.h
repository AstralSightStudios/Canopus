/* A target with the owner pump but no L2 transport (.036 / .139). */
#include <stdint.h>

#define CANOPUS_SUP_HAS_L2 0
#define CANOPUS_SUP_HAS_OWNER_PUMP 1

void *test_l2_timer(void (*)(void *), uint32_t, void *);
extern void *test_l2_loop;
int test_l2_owner_init(void *, void *);
int test_l2_owner_start(void *, void (*)(void *), uint64_t, uint64_t);
void test_l2_timer_delete(void *);
#define L2_PROBE_OWNER_LOOP ((uintptr_t)&test_l2_loop)
#define L2_PROBE_OWNER_TIMER_INIT ((uintptr_t)test_l2_owner_init)
#define L2_PROBE_OWNER_TIMER_START ((uintptr_t)test_l2_owner_start)
#define L2_PROBE_OWNER_TIMER_SIZE 136u
#define L2_PROBE_TIMER_DELETE ((uintptr_t)test_l2_timer_delete)
#define L2_PROBE_TIMER ((uintptr_t)test_l2_timer)
