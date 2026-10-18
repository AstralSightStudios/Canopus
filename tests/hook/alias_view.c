/* A code view like Band 11's: code runs at an instruction alias of RAM that is
 * written through a data view 0x20000000 higher. Only `view_owner` may patch the
 * window below; publish counts calls and returns `view_publish_result`. */
#include "canopus_hook.h"

#define ALIAS_CODE 0x1C100000u
#define ALIAS_CODE_SIZE 0x800u
#define ALIAS_TRAMPOLINES 0x1C100800u
#define ALIAS_DELTA 0x20000000u

uint32_t view_owner;
uint32_t view_publish_count;
uint32_t view_trampoline_calls;
int32_t view_publish_result;

static uintptr_t write_address(uint32_t slot, uintptr_t code, uint32_t length)
{
    if (slot != view_owner || code < ALIAS_CODE || code >= ALIAS_CODE + ALIAS_CODE_SIZE ||
        length > ALIAS_CODE + ALIAS_CODE_SIZE - code) {
        return 0u;
    }
    return code + ALIAS_DELTA;
}

static int trampoline_memory(uint32_t size, uintptr_t *exec, uintptr_t *write)
{
    view_trampoline_calls++;
    if (size > 0x200u) return -1;
    *exec = ALIAS_TRAMPOLINES;
    *write = ALIAS_TRAMPOLINES + ALIAS_DELTA;
    return 0;
}

static int publish(void)
{
    view_publish_count++;
    return view_publish_result;
}

const struct canopus_hook_code_view fixture_view = {
    write_address, trampoline_memory, publish,
};
