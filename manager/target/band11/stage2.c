#include "canopus_band11_memory.h"
#include "canopus_band11_sizes.h"
#include "canopus_elf32_loader.h"
#include "canopus_memory.h"
struct stage_state {
    struct canopus_elf_module module;
    volatile int32_t *mailbox;
    int region;
    uint32_t invoked;
};
static void *allocate(void *cookie, uint32_t size, uint32_t align, uint32_t *base) {
    void *p = b11_alloc(align, size); (void)cookie;
    if (p && !b11_heap_contains((uintptr_t)p, size)) { b11_free(p); return 0; }
    if (p) *base = (uint32_t)(uintptr_t)p;
    return p;
}
static void release(void *cookie, void *p, uint32_t size) {
    struct stage_state *s = cookie; (void)size;
    /* Constructors can publish callbacks/device nodes before reporting an
     * error. Keep their image resident once any constructor has run. */
    if (s->invoked) return;
    if (s->region >= 0) b11_unmap((uint32_t)s->region);
    b11_free(p);
}
static int finalize(void *cookie, void *p, uint32_t base, uint32_t size,
                    const struct canopus_elf_region *r, uint32_t count) {
    struct stage_state *s = cookie; uint32_t length;
    (void)p;
    if (count < 2 || count > 3 || r[0].kind != CANOPUS_ELF_REGION_EXEC || r[0].offset ||
        !b11_heap_contains(base, size)) return -1;
    length = r[0].size;
    if (r[1].kind == CANOPUS_ELF_REGION_RO) length += r[1].size;
    if (length > size) return -1;
    s->region = b11_map_exec(base, length);
    /* RW storage stays on the verified privileged SRAM background mapping. */
    return s->region < 0 ? -1 : 0;
}
static int invoke(void *cookie, uint32_t entry) {
    struct stage_state *s = cookie;
    s->invoked++;
    ((void (*)(volatile int32_t *))(uintptr_t)(entry | 1u))(s->mailbox);
    return 0;
}
__attribute__((used, section(".text.entry")))
int canopus_band11_stage2_entry(volatile int32_t *mailbox) {
    struct stage_state *s; struct canopus_elf_loader_ops ops;
    void *elf, *scratch; int rc;
    *mailbox = -310;
    s = b11_temp_alloc(8, sizeof(*s));
    if (!s) return -311;
    canopus_memset(s, 0, sizeof(*s)); s->region = -1; s->mailbox = mailbox;
    elf = b11_temp_alloc(8, B11_SUPERVISOR_SIZE);
    scratch = b11_temp_alloc(32, CANOPUS_ELF32_SCRATCH_SIZE);
    if (!elf || !scratch) { rc = -312; goto done; }
    if (b11_read_file("/data/canopus/supervisor.elf", elf, B11_SUPERVISOR_SIZE, B11_SUPERVISOR_CRC)) { rc = -313; goto done; }
    ops.cookie = s; ops.allocate = allocate; ops.release = release;
    ops.finalize = finalize; ops.invoke = invoke;
    *mailbox = -314;
    rc = canopus_elf32_load(elf, B11_SUPERVISOR_SIZE, &ops, &s->module, scratch);
    if (rc) rc -= 400;
    else rc = *mailbox;
done:
    b11_temp_free(scratch); b11_temp_free(elf); b11_temp_free(s);
    return rc;
}
