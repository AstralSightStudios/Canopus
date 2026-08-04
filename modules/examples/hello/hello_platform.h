/*
 * hello_platform.h — platform hooks for the hello example module.
 *
 * On host, hooks map to the fake target (CAN-C-009). On the real device
 * these will be generated typed veneers from the target pack (Phase 4);
 * a non-host build without veneers is an error, not a silent stub.
 */
#ifndef HELLO_PLATFORM_H
#define HELLO_PLATFORM_H

#ifdef CANOPUS_HOST
#include "fake_target.h"
#define hello_timer_register fake_timer_register
#define hello_timer_cancel fake_timer_cancel
#define hello_clock_gettime fake_clock_gettime
#else
#error "hello example: no platform hook for this build (generated veneers arrive in Phase 4)"
#endif

#endif /* HELLO_PLATFORM_H */
