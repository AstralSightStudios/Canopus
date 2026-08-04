/* Host test runner for the Canopus portable runtime. */
#include "canopus_test.h"

int run_abi_tests(void);
int run_control_tests(void);
int run_lifecycle_tests(void);
int run_resource_tests(void);
int run_diag_tests(void);
int run_hello_tests(void);
int run_supervisor_tests(void);
int run_manager_tests(void);
int run_app_sdk_tests(void);

int main(void)
{
    int total = 0;
    total += run_abi_tests();
    total += run_control_tests();
    total += run_lifecycle_tests();
    total += run_resource_tests();
    total += run_diag_tests();
    total += run_hello_tests();
    total += run_supervisor_tests();
    total += run_manager_tests();
    total += run_app_sdk_tests();
    if (total == 0) {
        printf("all host tests passed\n");
    } else {
        printf("host test failures: %d\n", total);
    }
    return total == 0 ? 0 : 1;
}
