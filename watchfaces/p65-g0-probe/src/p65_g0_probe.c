/* Minimal P65 G0 module: no heap, firmware imports, device calls or UI hooks. */
#include <stdint.h>

struct p65_modlib_unload_pair_v1 {
    void (*callback)(void *context);
    void *context;
};

_Static_assert(sizeof(struct p65_modlib_unload_pair_v1) == 8u,
               "P65 modlib unload pair must be two words");

static volatile uint32_t s_probe_marker;

static void p65_g0_probe_unload(void *context)
{
    (void)context;
    s_probe_marker = 0u;
}

__attribute__((used, noinline, section(".text.canopus_module_entry")))
int32_t canopus_module_initialize(struct p65_modlib_unload_pair_v1 *pair)
{
    if (pair == 0) return -1;
    pair->callback = 0;
    pair->context = 0;
    s_probe_marker = 0x47305036u; /* "G0P6" */
    pair->callback = p65_g0_probe_unload;
    pair->context = 0;
    return 0;
}
