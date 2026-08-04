/* Host tests: lifecycle state machine (CAN-C-005). */
#include "canopus_test.h"
#include "canopus_runtime.h"

static void drive_to_active(struct canopus_lifecycle_v1 *lc)
{
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_VERIFIED) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_INSTALLED) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_DISABLED) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_ENABLED) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_LOADING) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_PREPARING) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_READY) == 0);
    CHECK(canopus_lifecycle_transition(lc, CANOPUS_STATE_ACTIVE) == 0);
}

TEST(lifecycle_removable_full_path)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_REMOVABLE) == 0);
    CHECK_EQ(lc.state, CANOPUS_STATE_DISCOVERED);
    drive_to_active(&lc);
    CHECK_EQ(lc.state, CANOPUS_STATE_ACTIVE);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_STOPPING) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_DRAINING) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_UNLOADED) == 0);
    CHECK_EQ(lc.state, CANOPUS_STATE_UNLOADED);
    /* generation advanced on every transition */
    CHECK_EQ(lc.generation, 12u);
}

TEST(lifecycle_illegal_transition_rejected)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_REMOVABLE) == 0);
    /* DISCOVERED -> LOADING is not allowed */
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_LOADING) == -1);
    CHECK_EQ(lc.state, CANOPUS_STATE_DISCOVERED);
    /* jump to UNLOADED from DISCOVERED */
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_UNLOADED) == -1);
}

TEST(lifecycle_resident_barrier_blocks_unload)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION) == 0);
    drive_to_active(&lc);
    /* resident: ACTIVE -> BOOT_RESIDENT allowed */
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_BOOT_RESIDENT) == 0);
    /* no unload path after the barrier */
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_UNLOADED) == -1);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_DISABLED_NEXT_BOOT) == 0);
}

TEST(lifecycle_removable_cannot_go_boot_resident)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_REMOVABLE) == 0);
    drive_to_active(&lc);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_BOOT_RESIDENT) == -1);
}

TEST(lifecycle_resident_failstop_quarantine)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_ALWAYS_RESIDENT) == 0);
    drive_to_active(&lc);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_BOOT_RESIDENT) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_FAIL_STOP) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_QUARANTINED_NEXT_BOOT) == 0);
}

TEST(lifecycle_resident_update_requires_reboot)
{
    struct canopus_lifecycle_v1 lc;
    CHECK(canopus_lifecycle_init(&lc, CANOPUS_LIFECYCLE_RESIDENT_AFTER_ACTIVATION) == 0);
    drive_to_active(&lc);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_BOOT_RESIDENT) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_UPDATE_STAGED) == 0);
    CHECK(canopus_lifecycle_transition(&lc, CANOPUS_STATE_REBOOT_REQUIRED) == 0);
}

static struct test_registry lifecycle_tests[] = {
    { "lifecycle_removable_full_path", lifecycle_removable_full_path_wrapper },
    { "lifecycle_illegal_transition_rejected", lifecycle_illegal_transition_rejected_wrapper },
    { "lifecycle_resident_barrier_blocks_unload", lifecycle_resident_barrier_blocks_unload_wrapper },
    { "lifecycle_removable_cannot_go_boot_resident", lifecycle_removable_cannot_go_boot_resident_wrapper },
    { "lifecycle_resident_failstop_quarantine", lifecycle_resident_failstop_quarantine_wrapper },
    { "lifecycle_resident_update_requires_reboot", lifecycle_resident_update_requires_reboot_wrapper },
};
#define LIFECYCLE_TESTS_LEN (sizeof(lifecycle_tests) / sizeof(lifecycle_tests[0]))

int run_lifecycle_tests(void)
{
    RUN_TESTS(lifecycle_tests, LIFECYCLE_TESTS_LEN);
}
