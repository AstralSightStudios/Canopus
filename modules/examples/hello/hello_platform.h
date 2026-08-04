/*
 * hello_platform.h — platform hooks for the hello example module.
 *
 * Three build modes:
 *   CANOPUS_HOST    — host tests, hooks map to the fake target (CAN-C-009).
 *   CANOPUS_TARGET  — device build, hooks are generated typed veneers from
 *                     the target pack (CAN-TGT-005); zero undefined imports.
 *   neither         — an error, never a silent stub.
 */
#ifndef HELLO_PLATFORM_H
#define HELLO_PLATFORM_H

#if defined(CANOPUS_HOST)
#include "fake_target.h"
typedef struct fake_timespec hello_timespec_t;
#define hello_timer_register fake_timer_register
#define hello_timer_cancel fake_timer_cancel
#define hello_clock_gettime fake_clock_gettime
#define HELLO_HAS_TIMER 1

#elif defined(CANOPUS_TARGET)
#include "canopus_veneer.h"
typedef stock_timespec_t hello_timespec_t;
#define hello_clock_gettime canopus_fw_clock_gettime
#define HELLO_HAS_TIMER 0

#else
#error "hello example: define CANOPUS_HOST or CANOPUS_TARGET"
#endif

#endif /* HELLO_PLATFORM_H */
