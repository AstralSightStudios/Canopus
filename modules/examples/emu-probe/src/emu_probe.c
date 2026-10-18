/*
 * emu_probe.c — target module for emulator end-to-end tests (Band 10 Pro 043).
 *
 * Registers its descriptor (canopus_module_entry.h) and, when activated, writes its
 * version code to /data/canopus/probe.txt so a test can observe which
 * generation the Supervisor actually loaded.
 */
#include "canopus_module_entry.h"

#ifndef PROBE_VERSION
#define PROBE_VERSION 1
#endif
#ifndef PROBE_FAIL_LOAD
#define PROBE_FAIL_LOAD 0
#endif

static int probe_ok(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }

static int probe_activate(const struct canopus_context_v1 *ctx)
{
    typedef int (*open_fn)(const char *, int, ...);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    char text[3] = { (char)('0' + PROBE_VERSION), '\n', 0 };
    int fd;
    (void)ctx;
    fd = open_file("/data/canopus/probe.txt", 2 | 4, 0666);
    if (fd >= 0) {
        (void)canopus_fw_write(fd, text, 2);
        (void)canopus_fw_close(fd);
    }
    return 0;
}

static int probe_query(struct canopus_status_writer_v1 *w) { (void)w; return 0; }

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1), CANOPUS_ABI_MAJOR, CANOPUS_ABI_MINOR, 0u,
    "emu_probe", "1.0.0", "emu", CANOPUS_TARGET_ID,
    probe_ok, probe_activate, probe_ok, probe_ok, probe_query, 0, 0,
};

__attribute__((constructor)) static void probe_ctor(const struct canopus_context_v1 *ctx)
{
    if (!PROBE_FAIL_LOAD) (void)canopus_module_register(ctx, &s_descriptor);
}
