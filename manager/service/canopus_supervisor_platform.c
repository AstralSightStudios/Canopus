/* canopus_supervisor_platform.c — real device platform.
 *
 * Registers /dev/canopus exactly the way btpatch registers /dev/btpatch:
 * stock `register_driver` selected by the target veneer, with a 12-word file_operations array
 * whose read/write sides exchange CPC2 requests and responses. The generated
 * veneer selects the exact firmware entry points.
 *
 * Package staging verifies target-bound receipts and artifact digests before
 * committing a generation. Private L2 integration remains experimental.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_manager_target.h"
#include "canopus_target_config.h"
#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
#include "band11/canopus_band11_abi.h"
#endif
#if defined(CANOPUS_SUP_BAND11_BOOTSTRAP)
#define CANOPUS_SUP_RESIDENT_DRIVER 1
#endif
#include "canopus_installer_bundle.h"
#include "canopus_module_registration.h"
#include "canopus_runtime.h"
#include "canopus_veneer.h" /* canopus_fw_register_driver / canopus_fw_unregister_driver */
#include "canopus_memory.h"
#ifdef CANOPUS_SUP_CUSTOM_LOADER
#include "canopus_elf32_loader.h"
#endif
#include "sha256.h"

#define CANOPUS_SUP_DEVICE_PATH "/dev/canopus"
/* A separate pseudo-filesystem inode, not a symlink into /dev (Lua .139
 * resolves paths before checking the /dev, /proc and /sys deny prefixes).
 * Keep this outside /data, which is a mounted filesystem. */
#define CANOPUS_SUP_INSTALLER_PATH "/canopus/install"
#define CANOPUS_SUP_INBOX_ROOT "/data/canopus/inbox/"
/* Package kinds: generation A, generation B, and the staging area the
 * miwear client writes to. The client only ever writes the NEXT pair. */
enum { SUP_KIND_A = 0, SUP_KIND_B = 1, SUP_KIND_NEXT = 2, SUP_KIND_COUNT = 3 };
static const char *const s_receipt_suffix[SUP_KIND_COUNT] = {
    ".cmi", ".cmi.b", ".cmi.next"};
static const char *const s_module_suffix[SUP_KIND_COUNT] = {
    ".ko", ".ko.b", ".ko.next"};
#define CANOPUS_SUP_PATH_MAX 224u
#define CANOPUS_SUP_READ_CHUNK 512u
#define CANOPUS_SUP_NUTTX_O_RDONLY 1
#define CANOPUS_SUP_NUTTX_O_WRONLY 2u
#define CANOPUS_SUP_NUTTX_O_CREAT 0x4u
#define CANOPUS_SUP_NUTTX_O_RDWR (CANOPUS_SUP_NUTTX_O_RDONLY | CANOPUS_SUP_NUTTX_O_WRONLY)
#define CANOPUS_SUP_DEVICE_MODE 438u /* 0666 */
#define CANOPUS_SUP_FOPS_WORDS 12u   /* matches the stock file_operations table */
/* Next-boot registry persistence (see canopus_supervisor.h for the format). */
#define CANOPUS_SUP_REGISTRY_PATH "/data/canopus/registry.bin"
#define CANOPUS_SUP_REGISTRY_TMP_PATH "/data/canopus/registry.tmp"

/* Targets that expose a second allocator domain route the loader's throwaway
 * buffers there; everywhere else this collapses to the default heap. Only the
 * resident image needs the executable-capable domain. */
#ifndef canopus_fw_mm_memalign_scratch
#define canopus_fw_mm_memalign_scratch canopus_fw_mm_memalign_default
#define canopus_fw_mm_free_scratch canopus_fw_mm_free_default
#endif

#ifdef CANOPUS_SUP_CUSTOM_LOADER
struct sup_loaded_module {
    struct canopus_elf_module image;
    uint8_t mpu_regions[3];
    uint32_t mpu_region_count;
    /* Set when the image lives in the scratch domain and is executed through
     * an instruction-side alias rather than an MPU-leased executable heap. */
    uint32_t aliased;
};

static struct sup_loaded_module s_loaded_modules[CANOPUS_SUP_MODULE_SLOTS];
#endif

/* CAN-P2-002: the fops table is typed from the recovered layout instead of a
 * bare uint32_t[12]. The veneer's `file_operations` carries the exact device
 * byte layout (open/close/read/write at +0x0..+0xc, ioctl at +0x14, 0x30
 * total); a compile-time size assertion locks it to the 12-word table the
 * driver expects. */
static file_operations s_fops;
static file_operations s_installer_fops;

CANOPUS_STATIC_ASSERT(sizeof(file_operations) == 0x30u,
                      "file_operations must match the 12-word stock table");
CANOPUS_STATIC_ASSERT(CANOPUS_SUP_FOPS_WORDS * 4u == sizeof(file_operations),
                      "fops word count must equal the typed layout");

static int sup_register_driver_exact(const char *path, const void *fops,
                                     void *private_data)
{
    return CANOPUS_SUP_REGISTER_DRIVER(path, fops,
                                        CANOPUS_SUP_DEVICE_MODE,
                                        private_data);
}

static int sup_control_open(void *filep)
{
    (void)filep;
    return 0;
}

static int sup_control_close(void *filep)
{
    (void)filep;
    return 0;
}

static int32_t sup_control_read(void *filep, void *buffer, uint32_t count)
{
    (void)filep;
    (void)canopus_sup_l2_start();
    return canopus_supervisor_device_read(canopus_supervisor_get(), buffer, count);
}

static int32_t sup_control_write(void *filep, const void *buffer, uint32_t count)
{
    const struct canopus_module_registration_v1 *registration;
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    int rc;

    (void)filep;
    (void)canopus_sup_l2_start();
    if (canopus_module_registration_is_frame(buffer, count)) {
        registration = (const struct canopus_module_registration_v1 *)buffer;
        if (registration->descriptor == 0u ||
            registration->module_id[0] == 0u ||
            registration->module_id[CANOPUS_SUP_MODULE_ID_MAX - 1u] != 0u) {
            return -1;
        }
        rc = canopus_supervisor_register_descriptor(
            sup, (const char *)registration->module_id,
            (const struct canopus_module_descriptor_v1 *)(uintptr_t)
                registration->descriptor);
        return rc == 0 ? (int32_t)count : -1;
    }
    return canopus_supervisor_device_write(sup, buffer, count);
}

static int32_t sup_installer_read(void *filep, void *buffer, uint32_t count)
{
    (void)filep;
    (void)canopus_sup_l2_start();
    return canopus_supervisor_installer_read(canopus_supervisor_get(), buffer, count);
}

static int32_t sup_installer_write(void *filep, const void *buffer, uint32_t count)
{
    (void)filep;
    (void)canopus_sup_l2_start();
    return canopus_supervisor_installer_write(canopus_supervisor_get(), buffer, count);
}

static int sup_register_device(void *cookie)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int *(*errno_location_fn)(void);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    errno_location_fn errno_location =
        (errno_location_fn)(uintptr_t)CANOPUS_SUP_NUTTX_ERRNO_LOCATION;
    int *error_pointer;
    int error;
    int fd;
    int rc;

    (void)cookie;
    s_fops.open = (void *)(uintptr_t)&sup_control_open;
    s_fops.close = (void *)(uintptr_t)&sup_control_close;
    s_fops.read = (void *)(uintptr_t)&sup_control_read;
    s_fops.write = (void *)(uintptr_t)&sup_control_write;
    s_installer_fops.open = (void *)(uintptr_t)&sup_control_open;
    s_installer_fops.close = (void *)(uintptr_t)&sup_control_close;
    s_installer_fops.read = (void *)(uintptr_t)&sup_installer_read;
    s_installer_fops.write = (void *)(uintptr_t)&sup_installer_write;
    /* The exact firmware wrapper discards inode_reserve failures and returns
     * the inode-lock release result. Remove a stale same-name inode first so
     * repeated loader attempts cannot be misreported as successful register. */
#ifndef CANOPUS_SUP_RESIDENT_DRIVER
    (void)CANOPUS_SUP_UNREGISTER_DRIVER(CANOPUS_SUP_DEVICE_PATH);
    (void)CANOPUS_SUP_UNREGISTER_DRIVER(CANOPUS_SUP_INSTALLER_PATH);
#endif
#ifdef CANOPUS_SUP_RESIDENT_DRIVER
    /* Never replace a live resident Supervisor's device node. */
    fd = open_file(CANOPUS_SUP_DEVICE_PATH, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd >= 0) { (void)close_file(fd); return -17; }
    fd = open_file(CANOPUS_SUP_INSTALLER_PATH, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd >= 0) { (void)close_file(fd); return -17; }
#endif
    rc = sup_register_driver_exact(CANOPUS_SUP_DEVICE_PATH,
                                   (const void *)&s_fops, (void *)0);
    if (rc != 0) return rc;
    fd = open_file(CANOPUS_SUP_DEVICE_PATH, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd < 0) {
        error_pointer = errno_location();
        error = error_pointer != 0 ? *error_pointer : 0;
        if (error < 0) error = -error;
        if (error > CANOPUS_SUP_REGISTER_ERRNO_MAX) {
            error = CANOPUS_SUP_REGISTER_ERRNO_MAX;
        }
    #ifndef CANOPUS_SUP_RESIDENT_DRIVER
    (void)CANOPUS_SUP_UNREGISTER_DRIVER(CANOPUS_SUP_DEVICE_PATH);
#endif
        return -(CANOPUS_SUP_REGISTER_VERIFY_ERRNO_BASE + error);
    }
    (void)close_file(fd);
    rc = sup_register_driver_exact(CANOPUS_SUP_INSTALLER_PATH,
                                   (const void *)&s_installer_fops, (void *)0);
    if (rc != 0) return rc;
    fd = open_file(CANOPUS_SUP_INSTALLER_PATH, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd < 0) return -(CANOPUS_SUP_REGISTER_VERIFY_ERRNO_BASE + 2);
    (void)close_file(fd);
    (void)canopus_sup_l2_start();
    return 0;
}

static int sup_unregister_device(void *cookie)
{
    (void)cookie;
#ifdef CANOPUS_SUP_RESIDENT_DRIVER
    return -38; /* resident Supervisor: teardown is reboot, not an unverified ABI */
#else
    (void)CANOPUS_SUP_UNREGISTER_DRIVER(CANOPUS_SUP_INSTALLER_PATH);
    return CANOPUS_SUP_UNREGISTER_DRIVER(CANOPUS_SUP_DEVICE_PATH);
#endif
}

static const uint8_t s_firmware_sha256[32] =
    CANOPUS_SUP_FIRMWARE_SHA256_BYTES;

static int sup_token_char(uint8_t c)
{
    return (c >= (uint8_t)'a' && c <= (uint8_t)'z') ||
           (c >= (uint8_t)'0' && c <= (uint8_t)'9') ||
           c == (uint8_t)'_' || c == (uint8_t)'-' || c == (uint8_t)'.';
}

static int sup_make_rooted_path(char out[CANOPUS_SUP_PATH_MAX],
                                const char *root, const char *token,
                                const char *suffix)
{
    uint32_t used = 0u;
    uint32_t i;

    if (out == 0 || root == 0 || token == 0 || suffix == 0 ||
        token[0] == '\0') return -1;
    for (i = 0u; root[i] != '\0'; i++) {
        if (used + 1u >= CANOPUS_SUP_PATH_MAX) return -1;
        out[used++] = root[i];
    }
    for (i = 0u; token[i] != '\0'; i++) {
        if (i >= CANOPUS_SUP_MODULE_ID_MAX - 1u || !sup_token_char((uint8_t)token[i]) ||
            used + 1u >= CANOPUS_SUP_PATH_MAX) return -1;
        out[used++] = token[i];
    }
    for (i = 0u; suffix[i] != '\0'; i++) {
        if (used + 1u >= CANOPUS_SUP_PATH_MAX) return -1;
        out[used++] = suffix[i];
    }
    out[used] = '\0';
    return 0;
}

static int sup_make_path(char out[CANOPUS_SUP_PATH_MAX], const char *token,
                         const char *suffix)
{
    return sup_make_rooted_path(out, CANOPUS_SUP_INBOX_ROOT, token, suffix);
}

static int sup_read_exact(const char *path, void *buffer, uint32_t size,
                          uint32_t *failure)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    typedef int *(*errno_location_fn)(void);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    read_fn read_file = (read_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READ;
    errno_location_fn errno_location =
        (errno_location_fn)(uintptr_t)CANOPUS_SUP_NUTTX_ERRNO_LOCATION;
    uint8_t *bytes = (uint8_t *)buffer;
    uint32_t used = 0u;
    int fd = open_file(path, CANOPUS_SUP_NUTTX_O_RDONLY);

    if (fd < 0) {
        if (failure != 0) *failure = CANOPUS_SUP_ERR_STAGE_RECEIPT_OPEN;
        return -1;
    }
    while (used < size) {
        int32_t got = read_file(fd, bytes + used, size - used);
        if (got <= 0) {
            (void)close_file(fd);
            if (failure != 0) {
                if (got == 0) {
                    *failure = (uint32_t)CANOPUS_SUP_ERR_STAGE_RECEIPT_READ_BASE;
                } else {
                    int *errno_value = errno_location();
                    int error = errno_value != 0 ? *errno_value : 0;
                    *failure = (uint32_t)(CANOPUS_SUP_ERR_STAGE_RECEIPT_ERRNO_BASE - error);
                }
            }
            return -1;
        }
        used += (uint32_t)got;
    }
    (void)close_file(fd);
    return 0;
}

static int sup_registry_errno(void)
{
    typedef int *(*errno_location_fn)(void);
    errno_location_fn errno_location =
        (errno_location_fn)(uintptr_t)CANOPUS_SUP_NUTTX_ERRNO_LOCATION;
    int *value = errno_location();
    return value != 0 && *value > 0 ? *value : 0;
}

/* A NuttX call result as a service result: success keeps its value, failure
 * becomes the negated errno (a bare -1 when none was set). */
static int sup_negated_errno(int rc)
{
    int error;
    if (rc >= 0) return rc;
    error = sup_registry_errno();
    return error != 0 ? -error : CANOPUS_SVC_ERR_IO;
}

static void sup_registry_diag(uint32_t stage, int error)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    uint32_t count;
    uint32_t encoded_error;
    if (sup == 0) return;
    count = sup->flags & CANOPUS_SUP_DIAG_SAVE_COUNT_MASK;
    encoded_error = error > 0 ? (uint32_t)error : 0u;
    if (encoded_error > 0xFFFFu) encoded_error = 0xFFFFu;
    sup->flags = count |
        (stage & CANOPUS_SUP_DIAG_STAGE_MASK) |
        (encoded_error << CANOPUS_SUP_DIAG_ERRNO_SHIFT);
}

static void sup_registry_diag_success(void)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    uint32_t count;
    if (sup == 0) return;
    count = (sup->flags & CANOPUS_SUP_DIAG_SAVE_COUNT_MASK) >>
            CANOPUS_SUP_DIAG_SAVE_COUNT_SHIFT;
    if (count < 0xFFu) count++;
    sup->flags = count << CANOPUS_SUP_DIAG_SAVE_COUNT_SHIFT;
}

static int sup_bytes_equal(const uint8_t *left, const uint8_t *right,
                           uint32_t size)
{
    uint8_t diff = 0u;
    uint32_t i;
    for (i = 0u; i < size; i++) diff |= (uint8_t)(left[i] ^ right[i]);
    return diff == 0u;
}

static int sup_verify_file(const char *path, const uint8_t *expected,
                           uint32_t size)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    read_fn read_file = (read_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READ;
    uint8_t chunk[CANOPUS_SUP_READ_CHUNK];
    uint32_t used = 0u;
    int fd = open_file(path, CANOPUS_SUP_NUTTX_O_RDONLY);

    if (fd < 0) return -1;
    while (used < size) {
        uint32_t want = size - used;
        int32_t got;
        if (want > sizeof(chunk)) want = sizeof(chunk);
        got = read_file(fd, chunk, want);
        if (got <= 0 || !sup_bytes_equal(chunk, expected + used,
                                          (uint32_t)(got > 0 ? got : 0))) {
            (void)close_file(fd);
            return -1;
        }
        used += (uint32_t)got;
    }
    if (read_file(fd, chunk, 1u) != 0 || close_file(fd) < 0) return -1;
    return 0;
}

/* Whole-record file write (fixed-size registry). Writes exactly `size`
 * bytes; O_CREAT (bit 4) with the device mode. No O_TRUNC is needed because
 * the registry is a fixed 784-byte record and callers always rewrite it
 * whole through a freshly-unlinked temp name.
 *
 * O_CREAT is 0x4 on this firmware, NOT 0x10: fs_open.c (sub_C1C1238) gates
 * the mode argument on flag bit 4 (`TST.W R2, #4`), which is the NuttX
 * `file_vopen` O_CREAT check; bit 5 (0x10) is not the create bit. With the
 * old 0x10 the registry tmp open failed on a non-existent file and the
 * registry was never persisted — modules always vanished after reboot. */
static int sup_write_all(const char *path, const void *data, uint32_t size)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*write_fn)(int, const void *, uint32_t);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    write_fn write_file = (write_fn)(uintptr_t)CANOPUS_SUP_NUTTX_WRITE;
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t used = 0u;
    int fd;

    if (path == 0 || data == 0 || size == 0u) {
        return CANOPUS_SUP_ERR_REGISTRY_OPEN;
    }
    fd = open_file(path, CANOPUS_SUP_NUTTX_O_WRONLY | CANOPUS_SUP_NUTTX_O_CREAT,
                   CANOPUS_SUP_DEVICE_MODE);
    if (fd < 0) {
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_OPEN_TMP, sup_registry_errno());
        return CANOPUS_SUP_ERR_REGISTRY_OPEN;
    }
    while (used < size) {
        int32_t written = write_file(fd, bytes + used, size - used);
        if (written <= 0) {
            int error = sup_registry_errno();
            (void)close_file(fd);
            sup_registry_diag(CANOPUS_SUP_REG_STAGE_WRITE_TMP, error);
            return CANOPUS_SUP_ERR_REGISTRY_WRITE;
        }
        used += (uint32_t)written;
    }
    if (close_file(fd) < 0) {
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_CLOSE_TMP, sup_registry_errno());
        return CANOPUS_SUP_ERR_REGISTRY_CLOSE;
    }
    return 0;
}

static int sup_unlink_path(const char *path)
{
    typedef int (*unlink_fn)(const char *);
    unlink_fn unlink_file = (unlink_fn)(uintptr_t)CANOPUS_SUP_NUTTX_UNLINK;
    if (path == 0) {
        return -1;
    }
    return unlink_file(path);
}

static int sup_rename_path(const char *from, const char *to)
{
    typedef int (*rename_fn)(const char *, const char *);
    rename_fn rename_file = (rename_fn)(uintptr_t)CANOPUS_SUP_NUTTX_RENAME;
    if (from == 0 || to == 0) {
        return -1;
    }
    return rename_file(from, to);
}

static int sup_hash_artifact(const char *path, uint32_t expected_size,
                             uint8_t digest[32])
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    read_fn read_file = (read_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READ;
    SHA256_CTX sha;
    uint8_t chunk[CANOPUS_SUP_READ_CHUNK];
    uint32_t total = 0u;
    int fd = open_file(path, CANOPUS_SUP_NUTTX_O_RDONLY);

    if (fd < 0) return -1;
    sha256_init(&sha);
    while (total < expected_size) {
        uint32_t want = expected_size - total;
        int32_t got;
        if (want > sizeof(chunk)) want = sizeof(chunk);
        got = read_file(fd, chunk, want);
        if (got <= 0) { (void)close_file(fd); return -1; }
        sha256_update(&sha, chunk, (uint32_t)got);
        total += (uint32_t)got;
    }
    if (read_file(fd, chunk, 1u) != 0) { (void)close_file(fd); return -1; }
    (void)close_file(fd);
    sha256_final(&sha, digest);
    return 0;
}

static int sup_digest_equal(const uint8_t left[32], const uint8_t right[32])
{
    uint8_t diff = 0u;
    uint32_t i;
    for (i = 0u; i < 32u; i++) diff |= (uint8_t)(left[i] ^ right[i]);
    return diff == 0u;
}

static int sup_module_id_equal(const uint8_t fixed[CANOPUS_INSTALL_MODULE_ID_SIZE],
                               const char *token)
{
    uint32_t i;
    for (i = 0u; i < CANOPUS_INSTALL_MODULE_ID_SIZE; i++) {
        if (fixed[i] != (uint8_t)token[i]) return 0;
        if (fixed[i] == 0u) return 1;
    }
    return 0;
}

static int sup_verify_kind(
    const char *token, uint32_t kind, struct canopus_install_receipt_v1 *receipt,
    uint32_t *diagnostic)
{
    uint8_t digest[CANOPUS_INSTALL_DIGEST_SIZE];
    char receipt_path[CANOPUS_SUP_PATH_MAX];
    char module_path[CANOPUS_SUP_PATH_MAX];

    if (diagnostic == 0 || receipt == 0 || kind >= SUP_KIND_COUNT ||
        sup_make_path(receipt_path, token, s_receipt_suffix[kind]) != 0 ||
        sup_make_path(module_path, token, s_module_suffix[kind]) != 0) {
        if (diagnostic != 0) *diagnostic = CANOPUS_SUP_ERR_STAGE_PATH;
        return -1;
    }
    if (sup_read_exact(receipt_path, receipt, sizeof(*receipt), diagnostic) != 0) {
        return -1;
    }
    if (canopus_install_receipt_validate(receipt, CANOPUS_SUP_TARGET_ID,
                                         s_firmware_sha256) != 0 ||
        !sup_module_id_equal(receipt->module_id, token)) {
        *diagnostic = CANOPUS_SUP_ERR_STAGE_RECEIPT_INVALID;
        return -1;
    }
    if (sup_hash_artifact(module_path, receipt->artifact_size, digest) != 0 ||
        !sup_digest_equal(digest, receipt->artifact_sha256)) {
        *diagnostic = CANOPUS_SUP_ERR_STAGE_ARTIFACT;
        return -1;
    }
    *diagnostic = CANOPUS_SUP_ERR_NONE;
    return 0;
}

static int sup_stage_verify(void *cookie, const char *token, uint32_t *version,
                            uint32_t *lifecycle)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_install_receipt_v1 receipt;
    uint32_t diagnostic;
    (void)cookie;
    if (sup_verify_kind(token, SUP_KIND_NEXT, &receipt, &diagnostic) != 0) {
        sup->error_code = diagnostic;
        return -1;
    }
    *version = receipt.module_version;
    *lifecycle = receipt.lifecycle_class;
    return 0;
}

static int sup_gen_exists(void *cookie, const char *token, uint32_t gen)
{
    char path[CANOPUS_SUP_PATH_MAX];
    uint32_t magic;
    (void)cookie;
    if (gen > 1u || sup_make_path(path, token, s_receipt_suffix[gen]) != 0) return 0;
    return sup_read_exact(path, &magic, sizeof(magic), 0) == 0;
}

static int sup_gen_verify(void *cookie, const char *token, uint32_t gen,
                          uint32_t *version, uint32_t *lifecycle)
{
    struct canopus_install_receipt_v1 receipt;
    uint32_t diagnostic;
    (void)cookie;
    if (gen > 1u ||
        sup_verify_kind(token, gen, &receipt, &diagnostic) != 0) {
        return -1;
    }
    *version = receipt.module_version;
    *lifecycle = receipt.lifecycle_class;
    return 0;
}

/* Replaces generation `gen` with the staged pair. The receipt is the commit
 * record: it is removed first and renamed in last, so a partial move leaves a
 * generation that fails verification instead of one that verifies wrongly. */
static int sup_stage_commit(void *cookie, const char *token, uint32_t gen)
{
    char from_receipt[CANOPUS_SUP_PATH_MAX];
    char from_module[CANOPUS_SUP_PATH_MAX];
    char to_receipt[CANOPUS_SUP_PATH_MAX];
    char to_module[CANOPUS_SUP_PATH_MAX];
    (void)cookie;
    if (gen > 1u ||
        sup_make_path(from_receipt, token, s_receipt_suffix[SUP_KIND_NEXT]) != 0 ||
        sup_make_path(from_module, token, s_module_suffix[SUP_KIND_NEXT]) != 0 ||
        sup_make_path(to_receipt, token, s_receipt_suffix[gen]) != 0 ||
        sup_make_path(to_module, token, s_module_suffix[gen]) != 0) {
        return -1;
    }
    (void)sup_unlink_path(to_receipt);
    (void)sup_unlink_path(to_module);
    if (sup_rename_path(from_module, to_module) != 0) return -1;
    if (sup_rename_path(from_receipt, to_receipt) != 0) return -1;
    return 0;
}

#ifdef CANOPUS_SUP_CUSTOM_LOADER
static void *sup_loader_allocate(void *cookie, uint32_t size,
                                 uint32_t alignment, uint32_t *code_base,
                                 uint32_t *data_base)
{
    void *allocation;
#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
    struct sup_loaded_module *loaded = cookie;
    uint32_t alias;

    /* Prefer the scratch domain reached through the PSRAM instruction alias.
     * Kmem is the firmware's own heap with roughly 50 KiB to spare, and an
     * allocation it cannot serve panics the device rather than failing, so
     * taking a module image out of it starves the firmware as well as
     * capping module size. Umem has megabytes and needs no MPU lease. */
    loaded->aliased = 0u;
    allocation = canopus_fw_mm_memalign_scratch(alignment, size);
    if (allocation != 0) {
        alias = b11_exec_alias((uintptr_t)allocation, size);
        if (alias != 0u && (alias & 31u) == 0u) {
            loaded->aliased = 1u;
            *code_base = alias;
            *data_base = (uint32_t)(uintptr_t)allocation;
            return allocation;
        }
        canopus_fw_mm_free_scratch(allocation);
    }
    /* Fall back to the MPU-leased executable heap. */
#else
    (void)cookie;
#endif
    allocation = canopus_fw_mm_memalign_default(alignment, size);
    if (allocation != 0) {
        *code_base = (uint32_t)(uintptr_t)allocation;
        *data_base = (uint32_t)(uintptr_t)allocation;
    }
    return allocation;
}

static void sup_loader_release(void *cookie, void *allocation, uint32_t size)
{
    struct sup_loaded_module *loaded = cookie;
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    uint32_t slot = (uint32_t)(loaded - s_loaded_modules);
    (void)size;
    canopus_supervisor_release_services(sup, slot);
    sup->modules[slot].descriptor = 0;

#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
    if (loaded->aliased) {
        loaded->aliased = 0u;
        canopus_fw_mm_free_scratch(allocation);
        return;
    }
    while (loaded->mpu_region_count) b11_unmap(loaded->mpu_regions[--loaded->mpu_region_count]);
#else
    while (loaded->mpu_region_count != 0u) {
        uint32_t region;
        loaded->mpu_region_count--;
        region = loaded->mpu_regions[loaded->mpu_region_count];
        *(volatile uint32_t *)(uintptr_t)0xE000ED98u = region;
        *(volatile uint32_t *)(uintptr_t)0xE000EDA0u = 0u;
        __asm__ volatile("dsb sy\n"
                         "isb sy\n"
                         ::: "memory");
        canopus_fw_mpu_region_release(region);
    }
#endif
    canopus_fw_mm_free_default(allocation);
}

static int sup_loader_finalize(void *cookie, void *allocation,
                               uint32_t code_base, uint32_t data_base,
                               uint32_t size,
                               const struct canopus_elf_region *regions,
                               uint32_t region_count)
{
#ifdef CANOPUS_SUP_BAND11_BOOTSTRAP
    struct sup_loaded_module *loaded = cookie;
    uint32_t length; int id;
    (void)allocation;
    if (region_count < 2 || region_count > 3 || regions[0].kind != CANOPUS_ELF_REGION_EXEC ||
        regions[0].offset) return -1;
    length = regions[0].size;
    if (regions[1].kind == CANOPUS_ELF_REGION_RO) length += regions[1].size;
    if (length > size) return -1;
    if (loaded->aliased) {
        /* No lease: the privileged default Code map already makes the alias
         * executable, and the MPU is checked for an overriding region when
         * the alias is derived. Recompute it here so finalize never trusts a
         * base it did not verify, then push the bytes written through the
         * data view out to where instruction fetch will find them. */
        if (code_base == 0u || code_base != b11_exec_alias(data_base, size)) {
            return -1;
        }
        return b11_publish_code();
    }
    if (code_base != data_base || !b11_heap_contains(data_base, size)) return -1;
    id = b11_map_exec(data_base, length);
    if (id < 0) return -1;
    loaded->mpu_regions[loaded->mpu_region_count++] = (uint8_t)id;
    return 0;
#else
    struct sup_loaded_module *loaded = cookie;
    uint32_t i;
    uint32_t physical_count;
    uint32_t region;
    uint32_t base;
    uint32_t length;
    uint32_t access;
    uint32_t target_base = data_base;
    (void)allocation;
    (void)size;

    if (code_base != data_base) return -1; /* no second view on this target */
    if (region_count < 2u || region_count > 3u ||
        regions[0].kind != CANOPUS_ELF_REGION_EXEC) return -1;
    physical_count = regions[1].kind == CANOPUS_ELF_REGION_RO ?
                     region_count - 1u : region_count;
    for (i = 0u; i < physical_count; i++) {
        region = canopus_fw_mpu_region_allocate();
        if (region > 7u) goto fail;
        loaded->mpu_regions[loaded->mpu_region_count++] = (uint8_t)region;
        if (i == 0u && regions[1].kind == CANOPUS_ELF_REGION_RO) {
            base = target_base + regions[0].offset;
            length = regions[0].size + regions[1].size;
            access = 1u;
        } else {
            uint32_t logical = i +
                (regions[1].kind == CANOPUS_ELF_REGION_RO ? 1u : 0u);
            base = target_base + regions[logical].offset;
            length = regions[logical].size;
            access = regions[logical].kind == CANOPUS_ELF_REGION_EXEC ? 1u :
                     (regions[logical].kind == CANOPUS_ELF_REGION_RW ? 0u : 4u);
        }
        if (canopus_fw_mpu_region_configure(region, base, length, access, 1u) != 0) {
            goto fail;
        }
    }
    return 0;

fail:
    while (loaded->mpu_region_count != 0u) {
        uint32_t region;
        loaded->mpu_region_count--;
        region = loaded->mpu_regions[loaded->mpu_region_count];
        *(volatile uint32_t *)(uintptr_t)0xE000ED98u = region;
        *(volatile uint32_t *)(uintptr_t)0xE000EDA0u = 0u;
        __asm__ volatile("dsb sy\n"
                         "isb sy\n"
                         ::: "memory");
        canopus_fw_mpu_region_release(region);
    }
    return -1;
#endif
}

static int sup_loader_invoke(void *cookie, uint32_t callable)
{
    /* Init entries receive the module's service context in r0. Modules that
     * declare a void() entry ignore it. */
    typedef void (*entry_fn)(const struct canopus_context_v1 *);
    entry_fn entry = (entry_fn)(uintptr_t)(callable | 1u);
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    (void)cookie;
    entry(sup->loading_slot >= 0
              ? canopus_supervisor_context(sup, (uint32_t)sup->loading_slot)
              : 0);
    return 0;
}

/* The input image is parsed and copied out, never executed, so it belongs in
 * the scratch domain. Reading it into the executable heap is what exhausted
 * Kmem on .139: the artifact alone is half that heap's total size. */
static int sup_read_artifact(const char *path, uint32_t size, uint8_t **out)
{
    uint8_t *buffer = canopus_fw_mm_memalign_scratch(4u, size);
    uint32_t failure;

    if (buffer == 0) return -1;
    if (sup_read_exact(path, buffer, size, &failure) != 0) {
        canopus_fw_mm_free_scratch(buffer);
        return -1;
    }
    *out = buffer;
    return 0;
}

static int sup_load_custom(const char *path, uint32_t artifact_size,
                           uint32_t index)
{
    struct sup_loaded_module *loaded = &s_loaded_modules[index];
    struct canopus_elf_loader_ops ops;
    uint8_t *elf;
    void *scratch;
    int rc;

    if (loaded->image.allocation != 0) return CANOPUS_ELF_LOAD_INVALID;
    if (sup_read_artifact(path, artifact_size, &elf) != 0) {
        return CANOPUS_ELF_LOAD_NOMEM;
    }
    scratch = canopus_fw_mm_memalign_scratch(32u,
                                               CANOPUS_ELF32_SCRATCH_SIZE);
    if (scratch == 0) {
        canopus_fw_mm_free_scratch(elf);
        return CANOPUS_ELF_LOAD_NOMEM;
    }
    canopus_memset(loaded, 0, sizeof(*loaded));
    ops.cookie = loaded;
    ops.allocate = sup_loader_allocate;
    ops.release = sup_loader_release;
    ops.finalize = sup_loader_finalize;
    ops.invoke = sup_loader_invoke;
    rc = canopus_elf32_load(elf, artifact_size, &ops, &loaded->image, scratch);
    canopus_fw_mm_free_scratch(scratch);
    canopus_fw_mm_free_scratch(elf);
    return rc;
}
static void sup_unload_custom(uint32_t index)
{
    struct sup_loaded_module *loaded = &s_loaded_modules[index];
    struct canopus_elf_loader_ops ops;

    ops.cookie = loaded;
    ops.allocate = sup_loader_allocate;
    ops.release = sup_loader_release;
    ops.finalize = sup_loader_finalize;
    ops.invoke = sup_loader_invoke;
    canopus_elf32_unload(&ops, &loaded->image);
}
#endif

static int sup_load_module(void *cookie, uint32_t index,
                           const char *name, uint32_t lifecycle_class)
{
#ifndef CANOPUS_SUP_CUSTOM_LOADER
    typedef void *(*insmod_fn)(const char *, const char *);
    insmod_fn insmod = (insmod_fn)(uintptr_t)CANOPUS_SUP_INSMOD;
#endif
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_install_receipt_v1 receipt;
    char path[CANOPUS_SUP_PATH_MAX];
    const char *module_name;
#ifndef CANOPUS_SUP_CUSTOM_LOADER
    void *handle;
#endif

    (void)cookie;
    if (sup == 0 || index >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->modules[index].state == 0u) return -1;
    module_name = name != 0 ? name : (const char *)sup->modules[index].module_id;
    /* The inbox artifact is revalidated immediately before every insmod. A
     * subsequent target pack may promote this to a proven atomic fd loader;
     * this firmware's verified public modlib API accepts paths only. */
    if (!sup_module_id_equal(sup->modules[index].module_id, module_name) ||
        sup_make_path(path, module_name,
                      s_module_suffix[sup->modules[index].active_gen & 1u]) != 0 ||
        sup_verify_kind(module_name, sup->modules[index].active_gen & 1u,
                        &receipt, &sup->error_code) != 0 ||
        receipt.lifecycle_class != sup->modules[index].lifecycle_class ||
        receipt.lifecycle_class != lifecycle_class ||
        receipt.module_version != sup->modules[index].version) {
        return -1;
    }
    sup->loading_slot = (int32_t)index;
#ifdef CANOPUS_SUP_CUSTOM_LOADER
    {
        int load_rc = sup_load_custom(path, receipt.artifact_size, index);
        if (load_rc != 0) {
            sup->loading_slot = -1;
            /* A heap that cannot hold this image is the one failure the
             * operator can act on (shrink the module, free a slot), so it
             * gets its own code instead of the generic load error. */
            if (load_rc == CANOPUS_ELF_LOAD_NOMEM) {
                sup->error_code = CANOPUS_SUP_ERR_NOMEM;
            }
            return -1;
        }
    }
#else
    handle = insmod(path, module_name);
    if (handle == 0) {
        sup->loading_slot = -1;
        return -1;
    }
#endif
    sup->loading_slot = -1;
    if (sup->modules[index].descriptor == 0) {
        sup->error_code = CANOPUS_SUP_ERR_DESCRIPTOR_MISSING;
        canopus_supervisor_release_services(sup, index);
#ifdef CANOPUS_SUP_CUSTOM_LOADER
        sup_unload_custom(index);
#else
        /* Nothing references the image (no descriptor was published), so it
         * can be released; otherwise a retry under the same module name, such
         * as a fallback to the previous generation, fails with EEXIST. */
        {
            typedef int (*rmmod_fn)(void *);
            (void)((rmmod_fn)(uintptr_t)CANOPUS_SUP_RMMOD)(handle);
        }
#endif
        return -1;
    }
    return CANOPUS_STATE_READY;
}

/* Remove intent applied at boot: delete the module's inbox receipt and ELF.
 * The module id resolves through the (briefly registered) slot. */
static int sup_remove_artifact(void *cookie, uint32_t index)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    char path[CANOPUS_SUP_PATH_MAX];
    const char *module_id;
    uint32_t kind;

    (void)cookie;
    if (sup == 0 || index >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->modules[index].state == 0u) {
        return -1;
    }
    module_id = (const char *)sup->modules[index].module_id;
    for (kind = 0u; kind < SUP_KIND_COUNT; kind++) {
        if (sup_make_path(path, module_id, s_receipt_suffix[kind]) != 0) return -1;
        (void)sup_unlink_path(path);
        if (sup_make_path(path, module_id, s_module_suffix[kind]) != 0) return -1;
        (void)sup_unlink_path(path);
    }
    return 0;
}

static int sup_registry_persist(void *cookie, const uint8_t *data, uint32_t len)
{
    int rc;
    (void)cookie;
    if (data == 0 || len == 0u || len > CANOPUS_SUP_REGISTRY_SIZE) {
        return CANOPUS_SUP_ERR_REGISTRY_OPEN;
    }
    /* Keep a failed temp file as evidence. A successful rename consumes it. */
    (void)sup_unlink_path(CANOPUS_SUP_REGISTRY_TMP_PATH);
    rc = sup_write_all(CANOPUS_SUP_REGISTRY_TMP_PATH, data, len);
    if (rc != 0) return rc;
    if (sup_verify_file(CANOPUS_SUP_REGISTRY_TMP_PATH, data, len) != 0) {
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_VERIFY_TMP, sup_registry_errno());
        return CANOPUS_SUP_ERR_REGISTRY_VERIFY_TMP;
    }
    if (sup_rename_path(CANOPUS_SUP_REGISTRY_TMP_PATH,
                        CANOPUS_SUP_REGISTRY_PATH) != 0) {
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_RENAME, sup_registry_errno());
        return CANOPUS_SUP_ERR_REGISTRY_RENAME;
    }
    if (sup_verify_file(CANOPUS_SUP_REGISTRY_PATH, data, len) != 0) {
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_VERIFY_FINAL,
                          sup_registry_errno());
        return CANOPUS_SUP_ERR_REGISTRY_VERIFY_FINAL;
    }
    sup_registry_diag_success();
    return 0;
}

static int sup_registry_restore(void *cookie, uint8_t *data, uint32_t len)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    read_fn read_file = (read_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READ;
    uint32_t used = 0u;
    int fd;

    (void)cookie;
    if (data == 0 || len == 0u || len > CANOPUS_SUP_REGISTRY_SIZE) {
        return -1;
    }
    fd = open_file(CANOPUS_SUP_REGISTRY_PATH, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd < 0) {
        int error = sup_registry_errno();
        if (error == 2) return 1; /* ENOENT: fresh install */
        sup_registry_diag(CANOPUS_SUP_REG_STAGE_RESTORE_OPEN, error);
        return -1;
    }
    while (used < len) {
        int32_t got = read_file(fd, data + used, len - used);
        if (got <= 0) {
            int error = got < 0 ? sup_registry_errno() : 0;
            (void)close_file(fd);
            sup_registry_diag(CANOPUS_SUP_REG_STAGE_RESTORE_READ, error);
            return -1;
        }
        used += (uint32_t)got;
    }
    (void)close_file(fd);
    return 0;
}

static int sup_stage_package(void *cookie, const char *token, uint32_t stage)
{
#ifdef CANOPUS_SUP_WATCHFACE_DELETE
    typedef int (*watchface_delete_fn)(const char *);
    watchface_delete_fn delete_watchface =
        (watchface_delete_fn)(uintptr_t)CANOPUS_SUP_WATCHFACE_DELETE;
#endif
#ifdef CANOPUS_SUP_WATCHFACE_RESET
    /* reset_watchface(force): the argument is forwarded to the firmware's
     * refresh_cur_watchface. force == 0 rebuilds only when the current
     * watchface record actually changed, which is exactly the post-delete
     * case. Targets without a recovered reset callable skip the refresh. */
    typedef int (*watchface_reset_fn)(int);
    watchface_reset_fn watchface_reset =
        (watchface_reset_fn)(uintptr_t)CANOPUS_SUP_WATCHFACE_RESET;
#endif
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    int staged;

    (void)cookie;
    /* Payload-free INSTALL is split across separate miwear callbacks so each
     * transaction performs at most one app-registry/Launcher mutation before
     * returning to the event loop. Stage 0 registers Manager. Stages 1 and 2
     * invoke idempotent module callbacks: modules register app/pages first,
     * then publish their Launcher entries on the following event turn. */
    if (token == 0) {
        if (stage == 0u) return canopus_manager_native_install();
        if (stage == 1u || stage == 2u) {
            return canopus_supervisor_publish_native_apps(sup, stage);
        }
        return -1;
    }
    if (stage != 0u) return -1;
    if (sup == 0) return -1;
    /* Fresh install: verified package becomes generation A and the module is
     * registered disabled. Existing module: a strictly newer version goes to
     * the inactive generation and takes effect at the next load (staged = 1). */
    staged = canopus_supervisor_stage_token(sup, token);
    if (staged < 0) return -1;
#ifdef CANOPUS_SUP_HEADLESS
    /* Emulator test builds drive INSTALL from an NSH task, where the UI-side
     * notification and watchface calls below block; skip them. */
#ifdef CANOPUS_SUP_WATCHFACE_DELETE
    (void)delete_watchface;
#endif
#ifdef CANOPUS_SUP_WATCHFACE_RESET
    (void)watchface_reset;
#endif
    return staged;
#endif
    if (canopus_manager_native_notify_module_installed() < 0) {
        /* Registration has already committed a verified, disabled module.
         * Notification is a delivery side effect: preserve the installer page
         * for manual cleanup, but never roll back a valid installation merely
         * because the stock notification service rejects this message. */
        return staged;
    }
    /* The firmware watchface manager switches away before deleting an active
     * non-last watchface. Failure is non-fatal: installation remains complete
     * and the installer page can show diagnostics for manual removal. */
#ifdef CANOPUS_SUP_WATCHFACE_DELETE
    (void)delete_watchface(token);
#endif
#ifdef CANOPUS_SUP_WATCHFACE_RESET
    /* Keeps the stock watchface layout consistent after the deletion. */
    (void)watchface_reset(0);
#endif
    return staged;
}


/* ---- native staging inbox ------------------------------------------- */
#ifdef CANOPUS_SUP_NUTTX_OPENDIR
static int sup_list_packages(void *cookie, uint32_t index, char *id)
{
    typedef void *(*opendir_fn)(const char *);
    typedef const uint8_t *(*readdir_fn)(void *);
    typedef int (*closedir_fn)(void *);
    uint32_t seen = 0u;
    const uint8_t *entry;
    void *dir;
    (void)cookie;
    dir = ((opendir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPENDIR)(CANOPUS_SUP_INBOX_ROOT);
    if (dir == 0) return 1;
    while ((entry = ((readdir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READDIR)(dir)) != 0) {
        const char *name = (const char *)(entry + 1);
        uint32_t n = (uint32_t)canopus_strnlen(name, 48u);
        uint32_t i;
        if (n <= 9u || n > 25u || canopus_strcmp(name + n - 9u, ".cmi.next") != 0)
            continue;
        canopus_memset(id, 0, CANOPUS_SUP_MODULE_ID_MAX);
        for (i = 0u; i < n - 9u; i++) id[i] = name[i];
        id[i] = '\0';
        if (!canopus_install_module_id_valid((const uint8_t *)id)) continue;
        if (seen++ == index) {
            (void)((closedir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSEDIR)(dir);
            return 0;
        }
    }
    (void)((closedir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSEDIR)(dir);
    return 1;
}
#else
#define sup_list_packages 0
#endif

static int sup_validate_receipt(void *cookie, const void *receipt)
{
    (void)cookie;
    return canopus_install_receipt_validate(receipt, CANOPUS_SUP_TARGET_ID,
                                            s_firmware_sha256);
}

/* FS service hooks: plain NuttX file calls through the proven pointers. */
static int64_t sup_file_seek(void *cookie, int fd, int64_t offset, int32_t whence);
static int sup_file_close(void *cookie, int fd);

/* Maps the service flags onto the proven NuttX primitives. READ opens an
 * existing file; WRITE creates or truncates (O_TRUNC is not proven on these
 * firmwares, so the file is unlinked first); WRITE|READ keeps the contents;
 * APPEND keeps the contents and seeks to the end. */
static int sup_file_open(void *cookie, const char *path, uint32_t flags)
{
    typedef int (*open_fn)(const char *, int, ...);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    int fd;
    int access;
    if ((flags & CANOPUS_FS_WRITE) == 0u) {
        fd = open_file(path, CANOPUS_SUP_NUTTX_O_RDONLY);
        return sup_negated_errno(fd);
    }
    access = (flags & CANOPUS_FS_READ) != 0u ? CANOPUS_SUP_NUTTX_O_RDWR
                                              : CANOPUS_SUP_NUTTX_O_WRONLY;
    if ((flags & CANOPUS_FS_APPEND) == 0u &&
        ((flags & CANOPUS_FS_READ) == 0u || (flags & CANOPUS_FS_TRUNC) != 0u)) {
        (void)sup_unlink_path(path);
    }
    fd = open_file(path, access | CANOPUS_SUP_NUTTX_O_CREAT, CANOPUS_SUP_DEVICE_MODE);
    if (fd < 0) return sup_negated_errno(fd);
    if ((flags & CANOPUS_FS_APPEND) != 0u && sup_file_seek(cookie, fd, 0, CANOPUS_FS_SEEK_END) < 0) {
        (void)sup_file_close(cookie, fd);
        return CANOPUS_SVC_ERR_IO;
    }
    return fd;
}

static int32_t sup_file_read(void *cookie, int fd, void *buffer, uint32_t size)
{
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    (void)cookie;
    return ((read_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READ)(fd, buffer, size);
}

static int32_t sup_file_write(void *cookie, int fd, const void *buffer,
                              uint32_t size)
{
    typedef int32_t (*write_fn)(int, const void *, uint32_t);
    (void)cookie;
    return ((write_fn)(uintptr_t)CANOPUS_SUP_NUTTX_WRITE)(fd, buffer, size);
}

static int sup_file_close(void *cookie, int fd)
{
    typedef int (*close_fn)(int);
    (void)cookie;
    return ((close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE)(fd);
}

static int sup_file_remove(void *cookie, const char *path)
{
    (void)cookie;
    return sup_negated_errno(sup_unlink_path(path));
}

static int64_t sup_file_seek(void *cookie, int fd, int64_t offset, int32_t whence)
{
#ifdef CANOPUS_SUP_NUTTX_LSEEK
    typedef int64_t (*lseek_fn)(int, int64_t, int);
    (void)cookie;
    return ((lseek_fn)(uintptr_t)CANOPUS_SUP_NUTTX_LSEEK)(fd, offset, whence);
#else
    (void)cookie; (void)fd; (void)offset; (void)whence;
    return -1;
#endif
}

static int sup_file_rename_hook(void *cookie, const char *from, const char *to)
{
    (void)cookie;
    return sup_negated_errno(sup_rename_path(from, to));
}

/* A directory is anything opendir accepts; a file reports its size through
 * lseek. The firmware has no recovered mtime source, so mtime stays 0. */
static int sup_file_stat_hook(void *cookie, const char *path, void *out_stat)
{
    struct canopus_fs_stat *st = (struct canopus_fs_stat *)out_stat;
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    close_fn close_file = (close_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSE;
    int fd;
    (void)cookie;
    if (st == 0) return CANOPUS_SVC_ERR_ARGUMENT;
    canopus_memset(st, 0, sizeof(*st));
#ifdef CANOPUS_SUP_NUTTX_OPENDIR
    {
        typedef void *(*opendir_fn)(const char *);
        typedef int (*closedir_fn)(void *);
        void *dir = ((opendir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPENDIR)(path);
        if (dir != 0) {
            (void)((closedir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSEDIR)(dir);
            st->is_dir = 1u;
            return 0;
        }
    }
#endif
    fd = open_file(path, CANOPUS_SUP_NUTTX_O_RDONLY);
    if (fd < 0) return sup_negated_errno(fd);
#ifdef CANOPUS_SUP_NUTTX_LSEEK
    {
        typedef int64_t (*lseek_fn)(int, int64_t, int);
        int64_t sz = ((lseek_fn)(uintptr_t)CANOPUS_SUP_NUTTX_LSEEK)(fd, 0, CANOPUS_FS_SEEK_END);
        st->size = sz >= 0 ? (uint32_t)sz : 0u;
    }
#endif
    (void)close_file(fd);
    return 0;
}

#ifdef CANOPUS_SUP_NUTTX_MKDIR
static int sup_dir_mkdir(void *cookie, const char *path)
{
    typedef int (*mkdir_fn)(const char *, uint32_t);
    (void)cookie;
    /* The stock Bluetooth service creates /data/misc with mode 0777. */
    return sup_negated_errno(((mkdir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_MKDIR)(path, 0777u));
}
#define SUP_DIR_MKDIR sup_dir_mkdir
#else
#define SUP_DIR_MKDIR 0
#endif

#ifdef CANOPUS_SUP_NUTTX_OPENDIR
static void *sup_dir_open(void *cookie, const char *path)
{
    typedef void *(*opendir_fn)(const char *);
    (void)cookie;
    return ((opendir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPENDIR)(path);
}

static int sup_dir_read(void *cookie, void *dir, void *entry)
{
    typedef const uint8_t *(*readdir_fn)(void *);
    struct canopus_fs_dirent *de = (struct canopus_fs_dirent *)entry;
    const uint8_t *raw;
    (void)cookie;
    if (dir == 0 || de == 0) return -1;
    raw = ((readdir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_READDIR)(dir);
    if (raw == 0) return 1; /* EOF */
    canopus_memset(de, 0, sizeof(*de));
    canopus_memcpy(de->name, (const char *)raw + 2, 63);
    de->is_dir = (raw[0] == 2);
    return 0;
}

static int sup_dir_close(void *cookie, void *dir)
{
    typedef int (*closedir_fn)(void *);
    (void)cookie;
    if (dir == 0) return -1;
    return ((closedir_fn)(uintptr_t)CANOPUS_SUP_NUTTX_CLOSEDIR)(dir);
}
#define SUP_DIR_OPEN sup_dir_open
#define SUP_DIR_READ sup_dir_read
#define SUP_DIR_CLOSE sup_dir_close
#else
#define SUP_DIR_OPEN 0
#define SUP_DIR_READ 0
#define SUP_DIR_CLOSE 0
#endif

static void *sup_heap_realloc(void *cookie, void *ptr, uint32_t size)
{
    (void)cookie;
    (void)ptr;
    (void)size;
    return 0;
}

static int sup_reboot(void *cookie)
{
    (void)cookie;
    /* Cortex-M33 Application Interrupt and Reset Control Register (AIRCR)
     * VECTKEY (0x05FA0000) | SYSRESETREQ (1 << 2) triggers CPU/system reset */
    __asm__ volatile("dsb" ::: "memory");
    *(volatile uint32_t *)0xE000ED0Cu = 0x05FA0004u;
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    while (1) { __asm__ volatile("wfi"); }
    return 0;
}

#ifdef CANOPUS_SUP_CLOCK_GETTIME
static uint32_t sup_monotonic_ms(void *cookie)
{
    struct { int64_t sec; int32_t nsec; } ts = {0, 0};
    typedef int (*clock_fn)(uint32_t, void *);
    (void)cookie;
    /* CLOCK_MONOTONIC = 1; stock_timespec_t is {int64 sec, int32 nsec}. */
    (void)((clock_fn)(uintptr_t)CANOPUS_SUP_CLOCK_GETTIME)(1u, &ts);
    return (uint32_t)ts.sec * 1000u + (uint32_t)ts.nsec / 1000000u;
}
#define SUP_MONOTONIC_MS sup_monotonic_ms
#else
#define SUP_MONOTONIC_MS 0
#endif

#if defined(CANOPUS_SUP_HEAP_MALLOC) && defined(CANOPUS_SUP_HEAP_FREE)
static void *sup_heap_alloc(void *cookie, uint32_t size)
{
    typedef void *(*malloc_fn)(uint32_t);
    (void)cookie;
    return ((malloc_fn)(uintptr_t)CANOPUS_SUP_HEAP_MALLOC)(size);
}

static void sup_heap_release(void *cookie, void *ptr)
{
    /* 036/043 return int, Band 11 returns void; the result is unused. */
    typedef void (*free_fn)(void *);
    (void)cookie;
    ((free_fn)(uintptr_t)CANOPUS_SUP_HEAP_FREE)(ptr);
}
#define SUP_HEAP_ALLOC sup_heap_alloc
#define SUP_HEAP_RELEASE sup_heap_release
#else
#define SUP_HEAP_ALLOC 0
#define SUP_HEAP_RELEASE 0
#endif

#ifdef CANOPUS_SUP_HEADLESS
#define SUP_NOTIFY 0
#else
static int sup_notify(void *cookie, const char *source, const char *title, const char *body)
{
    (void)cookie;
    return canopus_manager_native_notify(source, title, body);
}
#define SUP_NOTIFY sup_notify
#endif

/* ---- stock hardware services ------------------------------------------
 *
 * Every hook below calls an exact-target firmware entry point generated into
 * canopus_target_config.h / canopus_band11_addresses.h. A target without the
 * macros gets a NULL hook and the service answers UNSUPPORTED; nothing here
 * returns a made-up value. All calls run on the module/UI task. The entry
 * points are static-recovered and restricted, not device probed. */

#define SUP_EBUSY (-16)
#define SUP_ENOENT (-2)
#define SUP_EINVAL (-22)
#define SUP_ENOMEM (-12)
#define SUP_ENOSYS (-38)

/* Battery: Band 10 Pro exposes three getters, Band 11 one snapshot copy. */
#if defined(CANOPUS_SUP_PM_BATTERY_READY) && defined(CANOPUS_SUP_PM_BATTERY_LEVEL) && \
    defined(CANOPUS_SUP_PM_BATTERY_CHARGING)
static int sup_battery_state(void *cookie, uint32_t *percent, int *charging)
{
    typedef int (*getter_fn)(void);
    (void)cookie;
    if (!((getter_fn)(uintptr_t)CANOPUS_SUP_PM_BATTERY_READY)()) return SUP_EBUSY;
    *percent = (uint32_t)((getter_fn)(uintptr_t)CANOPUS_SUP_PM_BATTERY_LEVEL)();
    *charging = ((getter_fn)(uintptr_t)CANOPUS_SUP_PM_BATTERY_CHARGING)() != 0;
    return 0;
}
#define SUP_BATTERY_STATE sup_battery_state
#elif defined(CANOPUS_SUP_PM_BATTERY_READY) && defined(CANOPUS_SUP_PM_BATTERY_SNAPSHOT)
static int sup_battery_state(void *cookie, uint32_t *percent, int *charging)
{
    typedef int (*ready_fn)(void);
    typedef uint32_t (*snapshot_fn)(uint32_t out[4]);
    uint32_t snapshot[4];
    (void)cookie;
    if (!((ready_fn)(uintptr_t)CANOPUS_SUP_PM_BATTERY_READY)()) return SUP_EBUSY;
    (void)((snapshot_fn)(uintptr_t)CANOPUS_SUP_PM_BATTERY_SNAPSHOT)(snapshot);
    *charging = (snapshot[0] & 0xFFu) != 0u;
    *percent = snapshot[1];
    return 0;
}
#define SUP_BATTERY_STATE sup_battery_state
#else
#define SUP_BATTERY_STATE 0
#endif

/* Screen state: 0 off, 1 always-on display, 2 on. */
#if defined(CANOPUS_SUP_SCREEN_GET_STATE) || defined(CANOPUS_SUP_SCREEN_STATE)
static int sup_screen_state(void *cookie)
{
    int state;
    (void)cookie;
#if defined(CANOPUS_SUP_SCREEN_GET_STATE)
    state = ((int (*)(void))(uintptr_t)CANOPUS_SUP_SCREEN_GET_STATE)();
#else
    state = *(volatile uint8_t *)(uintptr_t)CANOPUS_SUP_SCREEN_STATE;
#endif
    return state >= 0 && state <= 2 ? state : CANOPUS_SVC_ERR_IO;
}
#define SUP_SCREEN_STATE sup_screen_state
#else
#define SUP_SCREEN_STATE 0
#endif

#ifdef CANOPUS_SUP_SCREEN_TURN_ON
/* The stock wake-lock path turns the screen on with source 26. */
#define SUP_SCREEN_SOURCE_WAKELOCK 26
static const char s_screen_tag[] = "canopus";

static int sup_wake_screen(void *cookie)
{
    typedef int (*turn_on_fn)(const char *tag, int source);
    (void)cookie;
    (void)((turn_on_fn)(uintptr_t)CANOPUS_SUP_SCREEN_TURN_ON)(s_screen_tag,
                                                           SUP_SCREEN_SOURCE_WAKELOCK);
    return 0;
}
#define SUP_WAKE_SCREEN sup_wake_screen
#else
#define SUP_WAKE_SCREEN 0
#endif

/* Wake locks: stock PowerManager wakelock at level 6 keeps the screen on while
 * held. One native object per Supervisor slot, created on first use and kept
 * for the life of the Supervisor (the stock destroy path is not called). */
#if defined(CANOPUS_SUP_WAKELOCK_CREATE) && defined(CANOPUS_SUP_WAKELOCK_ACQUIRE) && \
    defined(CANOPUS_SUP_WAKELOCK_RELEASE)
#define SUP_WAKELOCK_LEVEL_KEEP_ON 6u
static void *s_wakelock_object[CANOPUS_WAKELOCK_MAX];
static const char s_wakelock_tag[] = "canopus";

static int sup_wakelock_acquire(void *cookie, uint32_t index, uint32_t flags)
{
    typedef void *(*create_fn)(uint32_t level_and_flags, const char *tag);
    typedef void (*lock_fn)(void *wakelock);
    (void)cookie;
    if (index >= CANOPUS_WAKELOCK_MAX) return SUP_EINVAL;
    /* A CPU-only hold has no stock mechanism; the screen lock keeps the CPU up. */
    if ((flags & CANOPUS_WAKELOCK_SCREEN) == 0u) return SUP_ENOSYS;
    if (s_wakelock_object[index] == 0) {
        s_wakelock_object[index] = ((create_fn)(uintptr_t)CANOPUS_SUP_WAKELOCK_CREATE)(
            SUP_WAKELOCK_LEVEL_KEEP_ON, s_wakelock_tag);
        if (s_wakelock_object[index] == 0) return SUP_ENOMEM;
    }
    ((lock_fn)(uintptr_t)CANOPUS_SUP_WAKELOCK_ACQUIRE)(s_wakelock_object[index]);
    return 0;
}

static int sup_wakelock_release(void *cookie, uint32_t index)
{
    typedef void (*lock_fn)(void *wakelock);
    (void)cookie;
    if (index >= CANOPUS_WAKELOCK_MAX || s_wakelock_object[index] == 0) return SUP_EINVAL;
    ((lock_fn)(uintptr_t)CANOPUS_SUP_WAKELOCK_RELEASE)(s_wakelock_object[index]);
    return 0;
}
#define SUP_WAKELOCK_ACQUIRE sup_wakelock_acquire
#define SUP_WAKELOCK_RELEASE sup_wakelock_release
#else
#define SUP_WAKELOCK_ACQUIRE 0
#define SUP_WAKELOCK_RELEASE 0
#endif

/* Stock system properties. property_get with a NULL default answers -22 for a
 * key that does not exist; it writes up to 255 bytes plus NUL. */
#if defined(CANOPUS_SUP_PROPERTY_GET)
static int sup_property_get(const char *key, char value[256])
{
    typedef int (*get_fn)(const char *key, char *value, const char *default_value);
    return ((get_fn)(uintptr_t)CANOPUS_SUP_PROPERTY_GET)(key, value, 0);
}

static int sup_get_property(void *cookie, const char *key, char *buf, uint32_t max_len)
{
    char value[256];
    int length, i;
    (void)cookie;
    length = sup_property_get(key, value);
    if (length < 0) return length == SUP_EINVAL ? SUP_ENOENT : length;
    if ((uint32_t)length > max_len - 1u) length = (int)(max_len - 1u);
    for (i = 0; i < length; i++) buf[i] = value[i];
    buf[length] = '\0';
    return length;
}
#define SUP_GET_PROPERTY sup_get_property
#else
#define SUP_GET_PROPERTY 0
#endif

/* Manual brightness through a Canopus MiWearScreen session: while the session
 * is started its auto-brightness/brightness-value override the user's. The
 * stock manual range is 31..255 (the settings clamp is 31), so 0% maps to the
 * stock minimum rather than a black screen. */
#if defined(CANOPUS_SUP_SCREEN_SESSION_CREATE) && defined(CANOPUS_SUP_SCREEN_SESSION_START) && \
    defined(CANOPUS_SUP_SCREEN_SESSION_STOP) && \
    defined(CANOPUS_SUP_SCREEN_SESSION_SET_AUTO_BRIGHTNESS) && \
    defined(CANOPUS_SUP_SCREEN_SESSION_SET_BRIGHTNESS_VALUE) && \
    defined(CANOPUS_SUP_SCREEN_SESSION_RESTORE_DEFAULT)
#define SUP_BRIGHTNESS_MIN 31u
#define SUP_BRIGHTNESS_MAX 255u
#define SUP_AUTO_BRIGHTNESS_DISABLED 2
static void *s_brightness_session;
static const char s_brightness_tag[] = "canopus.display";

static int sup_set_brightness(void *cookie, uint32_t percent)
{
    typedef void *(*create_fn)(const char *tag);
    typedef void (*start_fn)(void *session, int auto_stop);
    typedef void (*value_fn)(void *session, int value);
    (void)cookie;
    if (percent > 100u) return SUP_EINVAL;
    if (s_brightness_session == 0) {
        s_brightness_session = ((create_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_CREATE)(
            s_brightness_tag);
        if (s_brightness_session == 0) return SUP_ENOMEM;
    }
    ((value_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_SET_AUTO_BRIGHTNESS)(
        s_brightness_session, SUP_AUTO_BRIGHTNESS_DISABLED);
    ((value_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_SET_BRIGHTNESS_VALUE)(
        s_brightness_session,
        (int)(SUP_BRIGHTNESS_MIN +
              (percent * (SUP_BRIGHTNESS_MAX - SUP_BRIGHTNESS_MIN) + 50u) / 100u));
    ((start_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_START)(s_brightness_session, 0);
    return 0;
}

static int sup_restore_brightness(void *cookie)
{
    typedef void (*session_fn)(void *session);
    (void)cookie;
    if (s_brightness_session == 0) return 0;
    ((session_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_RESTORE_DEFAULT)(s_brightness_session);
    ((session_fn)(uintptr_t)CANOPUS_SUP_SCREEN_SESSION_STOP)(s_brightness_session);
    return 0;
}
#define SUP_SET_BRIGHTNESS sup_set_brightness
#define SUP_RESTORE_BRIGHTNESS sup_restore_brightness
#else
#define SUP_SET_BRIGHTNESS 0
#define SUP_RESTORE_BRIGHTNESS 0
#endif

#if defined(CANOPUS_SUP_PROPERTY_GET_INT32)
/* The manual setting is stored as persist.screen.brightness_value (31..255);
 * while auto-brightness is on it does not describe what is displayed. */
static int sup_get_brightness(void *cookie, uint32_t *percent)
{
    typedef int (*get_int_fn)(const char *key, int default_value);
    get_int_fn get_int = (get_int_fn)(uintptr_t)CANOPUS_SUP_PROPERTY_GET_INT32;
    int value;
    (void)cookie;
    if (get_int("persist.screen.auto_brightness", 0) != 0) return SUP_EBUSY;
    value = get_int("persist.screen.brightness_value", -1);
    if (value < 0) return SUP_ENOENT;
    if (value < 31) value = 31;
    if (value > 255) value = 255;
    *percent = ((uint32_t)(value - 31) * 100u + 112u) / 224u;
    return 0;
}
#define SUP_GET_BRIGHTNESS sup_get_brightness
#else
#define SUP_GET_BRIGHTNESS 0
#endif

/* Haptics. Band 10 Pro talks to the vibratord service through the stock HAL
 * client: a 132-byte message with the type at +4 and the argument at +8
 * (type 4 starts for duration_ms, type 5 stops). */
#if defined(CANOPUS_SUP_VIBRATOR_HAL_TRANSACT)
#define SUP_VIB_MSG_SIZE 132u
#define SUP_VIB_MSG_START 4u
#define SUP_VIB_MSG_STOP 5u
#define SUP_VIB_DISABLED (-138) /* the service's "vibration disabled" result */

static int sup_vib_transact(uint32_t type, uint32_t argument)
{
    typedef int (*transact_fn)(void *message);
    uint32_t message[SUP_VIB_MSG_SIZE / 4u];
    int rc;
    canopus_memset(message, 0, sizeof(message));
    ((uint8_t *)message)[4] = (uint8_t)type;
    message[2] = argument;
    rc = ((transact_fn)(uintptr_t)CANOPUS_SUP_VIBRATOR_HAL_TRANSACT)(message);
    return rc == SUP_VIB_DISABLED ? SUP_EBUSY : rc;
}

/* The service answers with the FF_GAIN write result (8 bytes) on success. */
static int sup_vibrate(void *cookie, uint32_t duration_ms)
{
    int rc;
    (void)cookie;
    rc = sup_vib_transact(SUP_VIB_MSG_START, duration_ms);
    return rc >= 0 ? 0 : rc;
}

static int sup_vibrate_cancel(void *cookie)
{
    int rc;
    (void)cookie;
    rc = sup_vib_transact(SUP_VIB_MSG_STOP, 0u);
    return rc >= 0 ? 0 : rc;
}
#define SUP_VIBRATE sup_vibrate
#define SUP_VIBRATE_CANCEL sup_vibrate_cancel
#elif defined(CANOPUS_SUP_VIBRATOR_RUN) && defined(CANOPUS_SUP_VIBRATOR_CANCEL)
/* Band 11 plays through miwear_vibrator_run: a 20-byte parameter block whose
 * kind byte (+9) 2 selects a waveform; +16 points at {count, segments}, each
 * 6-byte segment carrying its duration (u16 at +2) and amplitude (u8 at +5).
 * Priority 0 never preempts an exclusive stock vibration and plays once.
 * run returns a positive vibration id; cancel stops only that id. */
struct sup_vib_segment {
    uint16_t offset;
    uint16_t duration_ms;
    uint8_t flags;
    uint8_t amplitude;
};
struct sup_vib_waveform {
    int32_t count;
    const struct sup_vib_segment *segments;
};
struct sup_vib_params {
    uint8_t type;
    uint8_t reserved0[7];
    uint8_t priority;
    uint8_t kind;
    uint8_t reserved1[2];
    const char *key;
    const void *data;
};
CANOPUS_STATIC_ASSERT(sizeof(struct sup_vib_segment) == 6u, "stock waveform segment is 6 bytes");
CANOPUS_STATIC_ASSERT(sizeof(struct sup_vib_params) == 20u, "stock vibration parameters are 20 bytes");

#define SUP_VIB_KIND_WAVEFORM 2u
#define SUP_VIB_AMPLITUDE_MAX 255u
#define SUP_VIB_BUSY (-3) /* an exclusive stock vibration is playing */
static int s_vibration_id;

static int sup_vibrate(void *cookie, uint32_t duration_ms)
{
    typedef int (*run_fn)(const struct sup_vib_params *params);
    struct sup_vib_segment segment;
    struct sup_vib_waveform waveform;
    struct sup_vib_params params;
    int id;
    (void)cookie;
    if (duration_ms == 0u || duration_ms > 0xFFFFu) return SUP_EINVAL;
    canopus_memset(&segment, 0, sizeof(segment));
    segment.duration_ms = (uint16_t)duration_ms;
    segment.amplitude = SUP_VIB_AMPLITUDE_MAX;
    waveform.count = 1;
    waveform.segments = &segment;
    canopus_memset(&params, 0, sizeof(params));
    params.kind = SUP_VIB_KIND_WAVEFORM;
    params.data = &waveform;
    id = ((run_fn)(uintptr_t)CANOPUS_SUP_VIBRATOR_RUN)(&params);
    if (id <= 0) return id == SUP_VIB_BUSY ? SUP_EBUSY : (id < 0 ? id : CANOPUS_SVC_ERR_IO);
    s_vibration_id = id;
    return 0;
}

static int sup_vibrate_cancel(void *cookie)
{
    typedef void (*cancel_fn)(int id);
    (void)cookie;
    if (s_vibration_id > 0) {
        ((cancel_fn)(uintptr_t)CANOPUS_SUP_VIBRATOR_CANCEL)(s_vibration_id);
        s_vibration_id = 0;
    }
    return 0;
}
#define SUP_VIBRATE sup_vibrate
#define SUP_VIBRATE_CANCEL sup_vibrate_cancel
#else
#define SUP_VIBRATE 0
#define SUP_VIBRATE_CANCEL 0
#endif

/* Bluetooth adapter state: the stock stack's bt_adapter_get_state on its
 * adapter instance (0 off, 4 on). */
#if defined(CANOPUS_SUP_BT_ADAPTER_GET_STATE) && \
    (defined(CANOPUS_SUP_BT_ADAPTER_GET_INSTANCE) || defined(CANOPUS_SUP_BT_SHARED_ADAPTER))
static int sup_bt_adapter_state(void *cookie, uint32_t *state)
{
    typedef int (*state_fn)(void *adapter);
    void *adapter;
    int value;
    (void)cookie;
#if defined(CANOPUS_SUP_BT_ADAPTER_GET_INSTANCE)
    adapter = ((void *(*)(void))(uintptr_t)CANOPUS_SUP_BT_ADAPTER_GET_INSTANCE)();
#else
    adapter = *(void *volatile *)(uintptr_t)CANOPUS_SUP_BT_SHARED_ADAPTER;
#endif
    if (adapter == 0) return SUP_EBUSY;
    value = ((state_fn)(uintptr_t)CANOPUS_SUP_BT_ADAPTER_GET_STATE)(adapter);
    if (value < 0) return CANOPUS_SVC_ERR_IO;
    *state = (uint32_t)value;
    return 0;
}
#define SUP_BT_ADAPTER_STATE sup_bt_adapter_state
#else
#define SUP_BT_ADAPTER_STATE 0
#endif

#if !defined(CANOPUS_SUP_CUSTOM_LOADER) && !defined(CANOPUS_SUP_HEADLESS)
static void *s_service_timer;
static void (*s_service_timer_poll)(void *);
static void *s_service_timer_data;

static void sup_service_timer_tick(void *timer)
{
    (void)timer;
    s_service_timer_poll(s_service_timer_data);
}

static int sup_start_timer_pump(void *cookie, void (*poll)(void *), void *data)
{
    (void)cookie;
    if (s_service_timer != 0) {
        return s_service_timer_poll == poll && s_service_timer_data == data
                   ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_STATE;
    }
    s_service_timer_poll = poll;
    s_service_timer_data = data;
    /* The Supervisor and its heartbeat are boot-resident; neither may be hot-unloaded. */
    s_service_timer = canopus_fw_lv_timer_create(sup_service_timer_tick, 10u, 0);
    return s_service_timer != 0 ? CANOPUS_SVC_OK : CANOPUS_SVC_ERR_NOMEM;
}
#define SUP_TIMER_PUMP sup_start_timer_pump
#else
#define SUP_TIMER_PUMP 0
#endif

const struct canopus_sup_platform_v1 canopus_sup_platform = {
    CANOPUS_SUP_TARGET_ID,
    sup_register_device,
    sup_unregister_device,
    sup_load_module,
    sup_stage_package,
    sup_remove_artifact,
    sup_registry_persist,
    sup_registry_restore,
    sup_stage_verify,
    sup_stage_commit,
    sup_gen_exists,
    sup_gen_verify,
    sup_file_open,
    sup_file_read,
    sup_file_write,
    sup_file_seek,
    0, /* file_sync: no recovered fsync entry point */
    sup_file_close,
    sup_file_remove,
    sup_file_rename_hook,
    sup_file_stat_hook,
    SUP_DIR_MKDIR,
    SUP_DIR_OPEN,
    SUP_DIR_READ,
    SUP_DIR_CLOSE,
    sup_list_packages,
    SUP_MONOTONIC_MS,
    SUP_HEAP_ALLOC,
    sup_heap_realloc,
    SUP_HEAP_RELEASE,
    SUP_NOTIFY,
    sup_reboot,
    SUP_VIBRATE,
    SUP_VIBRATE_CANCEL,
    SUP_SET_BRIGHTNESS,
    SUP_GET_BRIGHTNESS,
    SUP_RESTORE_BRIGHTNESS,
    SUP_WAKE_SCREEN,
    SUP_SCREEN_STATE,
    SUP_WAKELOCK_ACQUIRE,
    SUP_WAKELOCK_RELEASE,
    SUP_BATTERY_STATE,
    SUP_GET_PROPERTY,
    SUP_BT_ADAPTER_STATE,
    sup_l2_connected,
    0, /* bt_rssi: no recovered stock source */
    SUP_TIMER_PUMP,
    sup_l2_send,
    sup_validate_receipt,
};
