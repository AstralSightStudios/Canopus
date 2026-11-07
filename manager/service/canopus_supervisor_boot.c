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
static uint32_t boot_proven_ms; /* when the finished sequence may clear boot.pending */
static int boot_lit;            /* the screen was lit since the sequence finished */
#define BOOT_STEP_MS 1000u   /* the installer's own Run paces its steps the same way */
#define BOOT_SETTLE_MS 3000u /* after the screen was last seen lit */
#define BOOT_PROVEN_MS 60000u

#if CANOPUS_BOOT_ENABLED
/* The startup script lives in the XIP image, and its 8 KiB erase block also
 * holds data the running firmware reads (a driver's file operations on Band 11
 * 4.100.155, UI tables on Band 10 Pro). The firmware's MTD erase sleeps once
 * the block is blank, so a task reading that data in the gap can fault, and a
 * band that restarts then finds the block blank. The block is therefore
 * rewritten with the flash HAL the MTD driver calls, erase and program back to
 * back with interrupts masked (BASEPRI 0x80, the driver's own level), and read
 * back through the uncached alias. The four block states are told apart by
 * CRC: the probe and the neutral form only clear bits, so they need no erase. */
#define BOOT_UNCACHED 0x28000000u
#define BOOT_TAIL (sizeof(boot_original) - 1u)
#define BOOT_TAIL_AT (CANOPUS_BOOT_FLASH_OFFSET - CANOPUS_BOOT_BLOCK_OFFSET)
#define BOOT_ATTEMPTS 3
#define BOOT_MIN_BATTERY 30u
#define BOOT_LOG "/data/canopus/pin.log"

enum { BOOT_UNKNOWN, BOOT_ORIGINAL, BOOT_PROBED, BOOT_PATCHED, BOOT_NEUTRAL };

typedef int (*boot_erase_fn)(int bank, uint32_t offset, uint32_t len);
typedef int (*boot_program_fn)(int bank, uint32_t offset, const void *data, uint32_t len,
                               uint8_t sync);
typedef int (*boot_flush_fn)(int bank, int kind);
#define BOOT_ERASE ((boot_erase_fn)(uintptr_t)CANOPUS_BOOT_FLASH_ERASE)
#define BOOT_PROGRAM ((boot_program_fn)(uintptr_t)CANOPUS_BOOT_FLASH_PROGRAM)
#define BOOT_FLUSH ((boot_flush_fn)(uintptr_t)CANOPUS_BOOT_FLASH_FLUSH)

static const volatile uint8_t *const boot_block =
    (const volatile uint8_t *)(uintptr_t)(BOOT_UNCACHED + CANOPUS_BOOT_BLOCK_OFFSET);

static uint32_t boot_crc32(const volatile uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu, k;
    while (n-- != 0u) {
        crc ^= *p++;
        for (k = 0; k < 8u; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static int boot_state(void)
{
    uint32_t crc = boot_crc32(boot_block, CANOPUS_BOOT_BLOCK_SIZE);
    if (crc == CANOPUS_BOOT_CRC_ORIGINAL) return BOOT_ORIGINAL;
    if (crc == CANOPUS_BOOT_CRC_PROBED) return BOOT_PROBED;
    if (crc == CANOPUS_BOOT_CRC_PATCHED) return BOOT_PATCHED;
    if (crc == CANOPUS_BOOT_CRC_NEUTRAL) return BOOT_NEUTRAL;
    return BOOT_UNKNOWN;
}

static uint32_t boot_mask(void)
{
    uint32_t saved;
    __asm volatile("mrs %0, basepri" : "=r"(saved));
    __asm volatile("msr basepri, %0" : : "r"(0x80u) : "memory");
    return saved;
}

static void boot_unmask(uint32_t saved)
{
    __asm volatile("msr basepri, %0" : : "r"(saved) : "memory");
}

/* Clears bits in place. */
static void boot_program(uint32_t at, const char *data, uint32_t len)
{
    uint32_t saved = boot_mask();
    (void)BOOT_PROGRAM(0, CANOPUS_BOOT_BLOCK_OFFSET + at, data, len, 1u);
    (void)BOOT_FLUSH(0, 1);
    boot_unmask(saved);
}

/* Nothing else runs between the erase and the last programmed byte. */
static void boot_rewrite(const uint8_t *block)
{
    uint32_t saved = boot_mask();
    (void)BOOT_ERASE(0, CANOPUS_BOOT_BLOCK_OFFSET, CANOPUS_BOOT_BLOCK_SIZE);
    (void)BOOT_FLUSH(0, 2);
    (void)BOOT_PROGRAM(0, CANOPUS_BOOT_BLOCK_OFFSET, block, CANOPUS_BOOT_BLOCK_SIZE, 1u);
    (void)BOOT_FLUSH(0, 1);
    boot_unmask(saved);
}

/* One "<step> <value>" line per step, synced, for a device retest. */
static void boot_note(struct canopus_supervisor_v1 *sup, int fd, const char *step, int32_t value)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    char line[40], digits[12];
    uint32_t n = 0u, d = 0u, v = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    if (fd < 0) return;
    while (*step != '\0' && n < 24u) line[n++] = *step++;
    line[n++] = ' ';
    if (value < 0) line[n++] = '-';
    do digits[d++] = (char)('0' + v % 10u); while ((v /= 10u) != 0u);
    while (d != 0u) line[n++] = digits[--d];
    line[n++] = '\n';
    (void)pf->file_write(sup->platform_cookie, fd, line, n);
    if (pf->file_sync != 0) (void)pf->file_sync(sup->platform_cookie, fd);
}

static int boot_rewrite_until(struct canopus_supervisor_v1 *sup, int fd, const uint8_t *block,
                              int want)
{
    int attempt, state = BOOT_UNKNOWN;
    for (attempt = 0; attempt < BOOT_ATTEMPTS && state != want; attempt++) {
        boot_rewrite(block);
        state = boot_state();
        boot_note(sup, fd, "rewrite", state);
    }
    return state;
}

/* Rewrites the block with the script tail replaced, from a copy of the block
 * as it stands; the copy must come out as the original or the pinned block.
 * A pin that does not take falls back to the original script. */
static int boot_commit(struct canopus_supervisor_v1 *sup, int fd, int pinned)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint8_t *block = (uint8_t *)pf->heap_alloc(sup->platform_cookie, CANOPUS_BOOT_BLOCK_SIZE);
    int want = pinned ? BOOT_PATCHED : BOOT_ORIGINAL, state;
    uint32_t i;
    if (block == 0) return CANOPUS_SVC_ERR_NOMEM;
    for (i = 0; i < CANOPUS_BOOT_BLOCK_SIZE; i++) block[i] = boot_block[i];
    canopus_memcpy(block + BOOT_TAIL_AT, pinned ? boot_patched : boot_original, BOOT_TAIL);
    if (boot_crc32(block, CANOPUS_BOOT_BLOCK_SIZE) !=
        (pinned ? CANOPUS_BOOT_CRC_PATCHED : CANOPUS_BOOT_CRC_ORIGINAL)) {
        boot_note(sup, fd, "copy", -1);
        pf->heap_release(sup->platform_cookie, block);
        return CANOPUS_SVC_ERR_STATE;
    }
    state = boot_rewrite_until(sup, fd, block, want);
    if (state != want && pinned) {
        canopus_memcpy(block + BOOT_TAIL_AT, boot_original, BOOT_TAIL);
        (void)boot_rewrite_until(sup, fd, block, BOOT_ORIGINAL);
    }
    pf->heap_release(sup->platform_cookie, block);
    return state == want ? 0 : CANOPUS_SVC_ERR_IO;
}

static int boot_status(struct canopus_supervisor_v1 *sup, uint32_t *status)
{
    *status = 0u;
    /* Without a background pump nothing could run the start sequence. */
    if (sup->platform->start_background_pump == 0) return 0;
    switch (boot_state()) {
    case BOOT_ORIGINAL:
    case BOOT_PROBED:
    case BOOT_NEUTRAL:
        *status = CANOPUS_BOOT_SUPPORTED;
        return 0;
    case BOOT_PATCHED:
        *status = CANOPUS_BOOT_SUPPORTED | CANOPUS_BOOT_PINNED;
        return 0;
    default:
        return CANOPUS_SVC_ERR_STATE;
    }
}

/* An erase that loses power leaves a blank block, so it waits for charge. */
static int boot_power(struct canopus_supervisor_v1 *sup, int fd)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    uint32_t percent = 0u;
    int charging = 0, rc;
    if (pf->battery_state == 0) return CANOPUS_SVC_ERR_UNSUPPORTED;
    rc = pf->battery_state(sup->platform_cookie, &percent, &charging);
    boot_note(sup, fd, rc != 0 ? "battery-error" : charging ? "charging" : "battery",
              rc != 0 ? rc : (int32_t)percent);
    if (rc != 0) return rc;
    return charging || percent >= BOOT_MIN_BATTERY ? 0 : CANOPUS_SVC_ERR_BUSY;
}

static int boot_pin(struct canopus_supervisor_v1 *sup, int fd, int state)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    static const char *const files[] = {
        "/data/canopus/boot.lua", "/data/canopus/boot.target",
        "/data/canopus/supervisor.elf", "/data/canopus/b"};
    struct canopus_fs_stat st;
    char target[48];
    int in = pf->file_open(sup->platform_cookie, files[1], CANOPUS_FS_READ), i, rc;
    if (in < 0) return in;
    canopus_memset(target, 0, sizeof(target));
    rc = pf->file_read(sup->platform_cookie, in, target, sizeof(target) - 1u);
    (void)pf->file_close(sup->platform_cookie, in);
    if (rc <= 0 || canopus_strcmp(target, pf->target_id) != 0) return CANOPUS_SVC_ERR_STATE;
    for (i = 0; i < 4; i++)
        if (pf->file_stat(sup->platform_cookie, files[i], &st) != 0) return CANOPUS_SVC_ERR_NOTFOUND;
    rc = boot_power(sup, fd);
    if (rc != 0) return rc;
    /* Before the first erase, a bits-only edit of the line the pin drops
     * shows the HAL programs this block and the read-back sees it. */
    if (state == BOOT_ORIGINAL) {
        boot_program(BOOT_TAIL_AT, CANOPUS_BOOT_PROBE, sizeof(CANOPUS_BOOT_PROBE) - 1u);
        state = boot_state();
        boot_note(sup, fd, "probe", state);
        if (state != BOOT_PROBED) return CANOPUS_SVC_ERR_IO;
    }
    return boot_commit(sup, fd, 1);
}

/* Unpinning comments the start line out in place; only a block that does not
 * read back as that is rewritten with the original script. */
static int boot_unpin(struct canopus_supervisor_v1 *sup, int fd)
{
    int state, rc;
    boot_program(CANOPUS_BOOT_NEUTRAL_AT, CANOPUS_BOOT_NEUTRAL, sizeof(CANOPUS_BOOT_NEUTRAL) - 1u);
    state = boot_state();
    boot_note(sup, fd, "neutral", state);
    if (state == BOOT_NEUTRAL) return 0;
    rc = boot_power(sup, fd);
    return rc != 0 ? rc : boot_commit(sup, fd, 0);
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
    int rc = boot_status(sup, &status), state, fd;
    if (rc != 0) return rc;
    if (!(status & CANOPUS_BOOT_SUPPORTED)) return CANOPUS_SVC_ERR_UNSUPPORTED;
    if (!!(status & CANOPUS_BOOT_PINNED) == pinned) return 0;
    state = boot_state();
    fd = pf->file_open(sup->platform_cookie, BOOT_LOG, CANOPUS_FS_WRITE);
    boot_note(sup, fd, pinned ? "pin" : "unpin", state);
    rc = pinned ? boot_pin(sup, fd, state) : boot_unpin(sup, fd);
    boot_note(sup, fd, "result", rc);
    if (fd >= 0) (void)pf->file_close(sup->platform_cookie, fd);
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
 * third-party module, so a module that faults still leaves it reachable (the
 * Supervisor's own start counts quarantine that module). The start counter in
 * boot.pending goes only once this start has outlived the whole sequence by a
 * minute and one screen wake: a fault that comes after the Manager exists, as
 * the screen callback on an exited `lua` did, must still count, or a pinned
 * band restarts forever. */
void canopus_supervisor_boot_poll(struct canopus_supervisor_v1 *sup)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    int rc = 0;
    uint32_t now = pf->monotonic_ms != 0 ? pf->monotonic_ms(sup->platform_cookie) : 0u;
    int lit;
    if (sup != boot_supervisor || (boot_phase == 0u && boot_proven_ms == 0u)) return;
    lit = pf->screen_state != 0 && pf->screen_state(sup->platform_cookie) != 0;
    if (boot_phase == 0u) {
        /* Sequence finished: wait for the minute and the wake. */
        if (lit) {
            boot_lit = 1;
        } else if (boot_lit && (int32_t)(now - boot_proven_ms) >= 0) {
            (void)pf->file_remove(sup->platform_cookie, "/data/canopus/boot.pending");
            boot_proven_ms = 0u;
        }
        return;
    }
    /* Registering the Manager while the screen is lit resets the emulated band
     * (Mirage), so the sequence only advances with the screen off. */
    if (lit) {
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
        boot_proven_ms = now + BOOT_PROVEN_MS;
        break;
    }
    if (rc != 0) {
        sup->error_code = (uint32_t)rc;
        boot_phase = 0u;
    }
}
