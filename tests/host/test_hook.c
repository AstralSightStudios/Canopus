/* Focused Hook Engine regressions; also runnable without the service test suite:
 * cc -DCANOPUS_HOST=1 -DCANOPUS_HOOK_STANDALONE_TEST=1 -Isdk/c -Iruntime/hook \
 *    runtime/hook/canopus_hook.c tests/host/test_hook.c -o /tmp/canopus-hook-test
 */
#include "canopus_test.h"
#include "canopus_hook.h"

#define FLASH_A ((uintptr_t)0x0C001001u)
#define FLASH_B ((uintptr_t)0x1C002001u)
#define FLASH_C ((uintptr_t)0x0C003001u)

static int original(int x) { return x + 1; }
static int replacement(int x) { return x + 10; }

static int call_slot(uintptr_t value, int arg)
{
    return ((int (*)(int))value)(arg);
}

TEST(pointer_redirect_and_restore)
{
    uintptr_t dispatch = (uintptr_t)&original;
    uintptr_t orig = 0;
    int32_t id;
    canopus_hook_engine_init();
    CHECK_EQ(canopus_hook_install(0, (uintptr_t)&original, (uintptr_t)&replacement,
                                  &orig, CANOPUS_HOOK_AUTO), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(call_slot(dispatch, 4), 5);
    id = canopus_hook_install(0, (uintptr_t)&dispatch, (uintptr_t)&replacement,
                              &orig, CANOPUS_HOOK_POINTER);
    CHECK(id >= 0);
    CHECK_EQ(orig, (uintptr_t)&original);
    CHECK_EQ(call_slot(dispatch, 4), 14);
    CHECK_EQ(call_slot(orig, 4), 5);
    CHECK_EQ(canopus_hook_uninstall(1, id), CANOPUS_SVC_ERR_NOTFOUND);
    CHECK_EQ(dispatch, (uintptr_t)&replacement);
    CHECK_EQ(canopus_hook_uninstall(0, id), CANOPUS_SVC_OK);
    CHECK_EQ(dispatch, (uintptr_t)&original);
    CHECK_EQ(canopus_hook_is_active(id), 0);
}

TEST(mapped_aliases_and_null_restore)
{
    uintptr_t dispatch = 0;
    uintptr_t other_dispatch = 0;
    uintptr_t orig = 123u;
    uintptr_t rejected_orig = 456u;
    int32_t id;
    canopus_hook_engine_init();
    CHECK_EQ(canopus_hook_register_slot_map(0, FLASH_A, (uintptr_t)&dispatch), 0);
    CHECK_EQ(canopus_hook_register_slot_map(1, FLASH_B, (uintptr_t)&dispatch), 0);
    id = canopus_hook_install(0, FLASH_A, (uintptr_t)&replacement, &orig,
                              CANOPUS_HOOK_AUTO);
    CHECK(id >= 0);
    CHECK_EQ(orig, 0);
    CHECK_EQ(call_slot(dispatch, 4), 14);
    CHECK_EQ(canopus_hook_is_hooked(FLASH_B), 1);
    CHECK_EQ(canopus_hook_is_hooked((uintptr_t)&dispatch), 1);
    CHECK_EQ(canopus_hook_install(1, FLASH_B, (uintptr_t)&original, &rejected_orig,
                                  CANOPUS_HOOK_AUTO), CANOPUS_SVC_ERR_STATE);
    CHECK_EQ(canopus_hook_install(1, (uintptr_t)&dispatch, (uintptr_t)&original,
                                  &rejected_orig, CANOPUS_HOOK_POINTER),
             CANOPUS_SVC_ERR_STATE);
    CHECK_EQ(rejected_orig, 456u);
    CHECK_EQ(canopus_hook_register_slot_map(0, FLASH_A & ~(uintptr_t)1u,
                                           (uintptr_t)&other_dispatch),
             CANOPUS_SVC_ERR_STATE);
    CHECK_EQ(canopus_hook_uninstall(0, id), 0);
    CHECK_EQ(dispatch, 0);
    CHECK_EQ(other_dispatch, 0);
    CHECK_EQ(canopus_hook_is_hooked(FLASH_B), 0);
}

TEST(direct_hook_blocks_mapped_alias)
{
    uintptr_t dispatch = (uintptr_t)&original;
    uintptr_t orig = 0;
    int32_t id;
    canopus_hook_engine_init();
    CHECK_EQ(canopus_hook_register_slot_map(1, FLASH_A, (uintptr_t)&dispatch), 0);
    id = canopus_hook_install(0, (uintptr_t)&dispatch, (uintptr_t)&replacement,
                              &orig, CANOPUS_HOOK_POINTER);
    CHECK(id >= 0);
    CHECK_EQ(canopus_hook_install(1, FLASH_A, (uintptr_t)&replacement, &orig,
                                  CANOPUS_HOOK_FPB), CANOPUS_SVC_ERR_STATE);
    canopus_hook_release_slot(0);
    CHECK_EQ(dispatch, (uintptr_t)&original);
    id = canopus_hook_install(1, FLASH_A, (uintptr_t)&replacement, &orig,
                              CANOPUS_HOOK_FPB);
    CHECK(id >= 0); /* Legacy request still redirects a registered RAM slot. */
    CHECK_EQ(orig, (uintptr_t)&original);
    CHECK_EQ(call_slot(dispatch, 4), 14);
    canopus_hook_release_slot(1);
    CHECK_EQ(dispatch, (uintptr_t)&original);
}

TEST(mapping_owner_release_does_not_retain_freed_ram)
{
    uintptr_t *owned = malloc(2u * sizeof(*owned));
    uintptr_t persistent = (uintptr_t)&original;
    uintptr_t orig = 0;
    int32_t id;
    canopus_hook_engine_init();
    CHECK(owned != NULL);
    if (owned == NULL) return;
    owned[0] = (uintptr_t)&original;
    owned[1] = 0;
    CHECK_EQ(canopus_hook_register_slot_map(0, FLASH_A, (uintptr_t)&owned[0]), 0);
    CHECK_EQ(canopus_hook_register_slot_map(0, FLASH_B, (uintptr_t)&owned[1]), 0);
    CHECK_EQ(canopus_hook_register_slot_map(1, FLASH_C, (uintptr_t)&persistent), 0);
    CHECK_EQ(canopus_hook_install(2, FLASH_A, (uintptr_t)&replacement, &orig,
                                  CANOPUS_HOOK_AUTO), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK(canopus_hook_install(0, FLASH_A, (uintptr_t)&replacement, &orig,
                               CANOPUS_HOOK_AUTO) >= 0);
    id = canopus_hook_install(1, FLASH_C, (uintptr_t)&replacement, &orig,
                              CANOPUS_HOOK_AUTO);
    CHECK(id >= 0);
    canopus_hook_release_slot(0);
    CHECK_EQ(owned[0], (uintptr_t)&original);
    CHECK_EQ(owned[1], 0);
    free(owned);
    /* Both installed and unused mappings must be gone before the owner frees. */
    CHECK_EQ(canopus_hook_install(0, FLASH_A, (uintptr_t)&replacement, &orig,
                                  CANOPUS_HOOK_AUTO), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(canopus_hook_install(0, FLASH_B, (uintptr_t)&replacement, &orig,
                                  CANOPUS_HOOK_AUTO), CANOPUS_SVC_ERR_UNSUPPORTED);
    CHECK_EQ(canopus_hook_is_hooked(FLASH_A), 0);
    CHECK_EQ(canopus_hook_is_hooked(FLASH_B), 0);
    CHECK_EQ(canopus_hook_is_active(id), 1);
    CHECK_EQ(call_slot(persistent, 4), 14);
    CHECK_EQ(canopus_hook_uninstall(1, id), 0);
    CHECK_EQ(persistent, (uintptr_t)&original);
    /* The released owner can register a fresh mapping without an old pointer. */
    CHECK_EQ(canopus_hook_register_slot_map(0, FLASH_A, (uintptr_t)&persistent), 0);
    CHECK(canopus_hook_install(0, FLASH_A, (uintptr_t)&replacement, &orig,
                               CANOPUS_HOOK_AUTO) >= 0);
    canopus_hook_release_slot(0);
    CHECK_EQ(persistent, (uintptr_t)&original);
    canopus_hook_release_slot(1);
}

TEST(unregistered_flash_is_unsupported)
{
    uintptr_t orig = 123u;
    int type;
    canopus_hook_engine_init();
    for (type = CANOPUS_HOOK_AUTO; type <= CANOPUS_HOOK_POINTER; type++) {
        CHECK_EQ(canopus_hook_install(0, FLASH_A, (uintptr_t)&replacement, &orig,
                                      (canopus_hook_type_t)type),
                 CANOPUS_SVC_ERR_UNSUPPORTED);
        CHECK_EQ(orig, 123u);
        CHECK_EQ(canopus_hook_is_hooked(FLASH_A), 0);
    }
}

int run_hook_tests(void)
{
    static const struct test_registry tests[] = {
        {"pointer_redirect_and_restore", pointer_redirect_and_restore_wrapper},
        {"mapped_aliases_and_null_restore", mapped_aliases_and_null_restore_wrapper},
        {"direct_hook_blocks_mapped_alias", direct_hook_blocks_mapped_alias_wrapper},
        {"mapping_owner_release_does_not_retain_freed_ram",
         mapping_owner_release_does_not_retain_freed_ram_wrapper},
        {"unregistered_flash_is_unsupported", unregistered_flash_is_unsupported_wrapper},
    };
    RUN_TESTS(tests, sizeof(tests) / sizeof(tests[0]));
}

#ifdef CANOPUS_HOOK_STANDALONE_TEST
int main(void) { return run_hook_tests(); }
#endif
