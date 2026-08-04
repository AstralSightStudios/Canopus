/*
 * fake_target.h — host fake target for running modules without hardware.
 *
 * Provides a deterministic allocator, timer wheel, callback registry and a
 * fake clock_gettime so module glue and the portable runtime can be host
 * tested end-to-end (CAN-C-009).
 */
#ifndef FAKE_TARGET_H
#define FAKE_TARGET_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- allocator ---------------------------------------------------- */

/* Allocates from a fixed arena. Tracks live count for leak checks. */
void *fake_alloc(size_t n);
void fake_free(void *p);
/* Number of live (allocated, not freed) allocations. Must be 0 after a
 * module is released for a clean test. */
size_t fake_alloc_live_count(void);
void fake_alloc_reset(void);

/* ---- clock -------------------------------------------------------- */

struct fake_timespec {
    int32_t tv_sec;
    int32_t tv_nsec;
};

/* Mirrors the stock clock_gettime(clock_id=1) contract: returns 0 and
 * writes a valid monotonic timespec. */
int fake_clock_gettime(uint32_t clock_id, struct fake_timespec *ts);

/* ---- timer -------------------------------------------------------- */

typedef void (*fake_timer_cb)(void *cookie);

#define FAKE_TIMER_MAX 8

/* Registers a periodic timer; returns a timer id >= 0, or -1 when full. */
int fake_timer_register(fake_timer_cb cb, void *cookie, uint32_t period_ms);
/* Cancels a timer. Returns 0 on success, -1 if unknown/idempotent-cancel. */
int fake_timer_cancel(int timer_id);
/* Fires all due timers once. The module's callbacks must not block. */
void fake_timer_fire_due(void);
size_t fake_timer_active_count(void);

/* ---- driver namespace --------------------------------------------- */

#define FAKE_DRIVER_MAX 4
#define FAKE_DRIVER_NAME_MAX 32

/* Returns 0 on success; -1 on name collision or full table. */
int fake_driver_register(const char *name, const void *ops, void *private_data);
/* Returns 0 on success; returns -16 (like the stock unregister EBUSY path)
 * when the name exists but a simulated open reference holds it, and -1 when
 * unknown. */
int fake_driver_unregister(const char *name);
/* Simulates an open reference on a driver (blocks unregister with -16). */
int fake_driver_hold(const char *name);
int fake_driver_release(const char *name);
size_t fake_driver_count(void);

#ifdef __cplusplus
}
#endif

#endif /* FAKE_TARGET_H */
