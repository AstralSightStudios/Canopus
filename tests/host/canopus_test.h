/*
 * canopus_test.h — tiny host test harness.
 */
#ifndef CANOPUS_TEST_H
#define CANOPUS_TEST_H

#include <stdio.h>
#include <stdlib.h>

typedef void (*test_fn)(void);

struct test_registry {
    const char *name;
    test_fn fn;
};

#define TEST(name) \
    static void name(void); \
    static void name##_wrapper(void) { \
        test_set_current(#name); \
        name(); \
    } \
    static void name(void)

static const char *g_current = "";
static int g_checks = 0;
static int g_failures = 0;

static inline void test_set_current(const char *n) { g_current = n; }

#define CHECK(cond) \
    do { \
        g_checks++; \
        if (!(cond)) { \
            g_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s (%s)\n", \
                    __FILE__, __LINE__, #cond, g_current); \
        } \
    } while (0)

#define CHECK_EQ(a, b) \
    do { \
        long long _va = (long long)(a); \
        long long _vb = (long long)(b); \
        g_checks++; \
        if (_va != _vb) { \
            g_failures++; \
            fprintf(stderr, "FAIL %s:%d: %s == %s (%lld vs %lld) (%s)\n", \
                    __FILE__, __LINE__, #a, #b, _va, _vb, g_current); \
        } \
    } while (0)

#define RUN_TESTS(regs, count) \
    do { \
        int _i; \
        int _n = (int)(count); \
        for (_i = 0; _i < _n; _i++) { \
            (regs)[_i].fn(); \
        } \
        if (g_failures == 0) { \
            printf("PASS %d checks across %d tests\n", g_checks, _n); \
            return 0; \
        } \
        printf("FAIL %d/%d checks\n", g_failures, g_checks); \
        return 1; \
    } while (0)

#endif /* CANOPUS_TEST_H */
