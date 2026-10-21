#include "canopus_supervisor_boot.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_services.h"
#include "canopus_memory.h"

#ifdef CANOPUS_HOST
#define CANOPUS_BOOT_ENABLED 0
#else
#include "canopus_boot_config.h"
#endif

static struct canopus_supervisor_v1 *volatile boot_supervisor;
static volatile uint32_t boot_phase;
static int boot_started;
static uint32_t boot_next_ms;
#define BOOT_STEP_MS 1000u   /* the installer's own Run paces its steps the same way */
#define BOOT_SETTLE_MS 3000u /* after the screen was last seen lit */

#if CANOPUS_BOOT_ENABLED
#define BOOT_FLASH "/dev/bes_flash"
#define BOOT_TAIL (sizeof(boot_original) - 1u)

static int boot_same(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < BOOT_TAIL; i++)
        if (a[i] != b[i]) return 0;
    return 1;
}

/* The script's differing tail goes through the firmware's MTD file proxy: an
 * FTL read-modify-write of the 8 KiB sector, flushed when the file is closed. */
static int boot_access(struct canopus_supervisor_v1 *sup, uint32_t flags, char *read,
                       const char *write)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    void *cookie = sup->platform_cookie;
    int fd = pf->file_open(cookie, BOOT_FLASH, flags), rc = 0;
    if (fd < 0) return fd;
    if (pf->file_seek(cookie, fd, CANOPUS_BOOT_FLASH_OFFSET, CANOPUS_FS_SEEK_SET) !=
        (int64_t)CANOPUS_BOOT_FLASH_OFFSET) {
        rc = CANOPUS_SVC_ERR_IO;
    } else if (write != 0 ? pf->file_write(cookie, fd, write, BOOT_TAIL) != (int32_t)BOOT_TAIL
                          : pf->file_read(cookie, fd, read, BOOT_TAIL) != (int32_t)BOOT_TAIL) {
        rc = CANOPUS_SVC_ERR_IO;
    }
    if (pf->file_close(cookie, fd) != 0 && rc == 0) rc = CANOPUS_SVC_ERR_IO;
    return rc;
}

static int boot_status(struct canopus_supervisor_v1 *sup, uint32_t *status)
{
    char tail[BOOT_TAIL];
    int rc;
    *status = 0u;
    /* Without a background pump nothing could run the start sequence. */
    if (sup->platform->start_background_pump == 0) return 0;
    rc = boot_access(sup, CANOPUS_FS_READ, tail, 0);
    if (rc == CANOPUS_SVC_ERR_NOTFOUND || rc == CANOPUS_SVC_ERR_UNSUPPORTED) return 0;
    if (rc != 0) return rc;
    if (boot_same(tail, boot_original)) {
        *status = CANOPUS_BOOT_SUPPORTED;
    } else if (boot_same(tail, boot_patched)) {
        *status = CANOPUS_BOOT_SUPPORTED | CANOPUS_BOOT_PINNED;
    } else {
        return CANOPUS_SVC_ERR_STATE;
    }
    return 0;
}
#endif

/* Bits 8.. report the start sequence's current step, 0 when idle. */
int canopus_supervisor_boot_query(struct canopus_supervisor_v1 *sup, uint32_t *status)
{
    int rc = 0;
#if CANOPUS_BOOT_ENABLED
    rc = boot_status(sup, status);
#else
    (void)sup;
    *status = 0u;
#endif
    *status |= boot_phase << 8;
    return rc;
}

int canopus_supervisor_boot_set(struct canopus_supervisor_v1 *sup, int pinned)
{
#if CANOPUS_BOOT_ENABLED
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint32_t status;
    int rc = boot_status(sup, &status);
    if (rc != 0) return rc;
    if (!(status & CANOPUS_BOOT_SUPPORTED)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (!!(status & CANOPUS_BOOT_PINNED) == pinned) return 0;
    if (pinned) {
        static const char *const files[] = {
            "/data/canopus/boot.lua", "/data/canopus/boot.target",
            "/data/canopus/supervisor.elf", "/data/canopus/b"};
        struct canopus_fs_stat st;
        char target[48];
        int fd = pf->file_open(sup->platform_cookie, files[1], CANOPUS_FS_READ), i;
        if (fd < 0) return fd;
        canopus_memset(target, 0, sizeof(target));
        rc = pf->file_read(sup->platform_cookie, fd, target, sizeof(target) - 1u);
        (void)pf->file_close(sup->platform_cookie, fd);
        if (rc <= 0 || canopus_strcmp(target, pf->target_id) != 0) return CANOPUS_SVC_ERR_STATE;
        for (i = 0; i < 4; i++)
            if (pf->file_stat(sup->platform_cookie, files[i], &st) != 0) return CANOPUS_SVC_ERR_NOTFOUND;
    }
    rc = boot_access(sup, CANOPUS_FS_RDWR, 0, pinned ? boot_patched : boot_original);
    if (rc == 0) {
        rc = boot_status(sup, &status);
        if (rc == 0 && !!(status & CANOPUS_BOOT_PINNED) != pinned) rc = CANOPUS_SVC_ERR_IO;
    }
    return rc;
#else
    (void)sup; (void)pinned;
    return CANOPUS_SVC_ERR_UNSUPPORTED;
#endif
}

int canopus_supervisor_boot_start(struct canopus_supervisor_v1 *sup)
{
    int rc;
    if (boot_started) return 0;
    boot_supervisor = sup;
    if (sup->platform->monotonic_ms != 0)
        boot_next_ms = sup->platform->monotonic_ms(sup->platform_cookie) + BOOT_STEP_MS;
    boot_phase = 1u;
    rc = canopus_supervisor_start_background(sup);
    if (rc != 0) {
        boot_phase = 0u;
        return rc;
    }
    boot_started = 1;
    return 0;
}

/* One step per second on the owner timer. The Manager comes before any
 * third-party module, so a module that faults still leaves it reachable, and
 * the start counter is cleared as soon as it exists. */
void canopus_supervisor_boot_poll(struct canopus_supervisor_v1 *sup)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    int rc = 0;
    uint32_t now = pf->monotonic_ms != 0 ? pf->monotonic_ms(sup->platform_cookie) : 0u;
    if (sup != boot_supervisor || boot_phase == 0u) return;
    /* Registering the Manager while the screen is lit resets the emulated band
     * (Mirage), so the sequence only advances with the screen off. */
    if (pf->screen_state != 0 && pf->screen_state(sup->platform_cookie) != 0) {
        boot_next_ms = now + BOOT_SETTLE_MS;
        return;
    }
    if ((int32_t)(now - boot_next_ms) < 0) return;
    boot_next_ms = now + BOOT_STEP_MS;
    switch (boot_phase++) {
    case 1u:
        rc = pf->stage_package(sup->platform_cookie, 0, 0u);
        break;
    case 2u:
        rc = pf->stage_package(sup->platform_cookie, 0, 2u);
        if (rc == 0) (void)pf->file_remove(sup->platform_cookie, "/data/canopus/boot.pending");
        break;
    case 3u:
        (void)canopus_supervisor_activate_restored_modules(sup);
        break;
    case 4u:
    case 5u:
        rc = pf->stage_package(sup->platform_cookie, 0, boot_phase - 4u);
        break;
    default:
        boot_phase = 0u;
        break;
    }
    if (rc != 0) {
        sup->error_code = (uint32_t)rc;
        boot_phase = 0u;
    }
}
