/* canopus_supervisor_platform.c — real device platform.
 *
 * Registers /dev/canopus exactly the way btpatch registers /dev/btpatch:
 * stock `register_driver` (0x0C1A0D51) with a 12-word file_operations array
 * whose read side renders the 384-byte status ABI and whose write side
 * dispatches the 16-byte command ABI. This is the same managed symbol the
 * target pack exposes via the generated veneer (`canopus_fw_register_driver`).
 *
 * Loading/unloading Canopus modules and package staging remain fail-closed
 * until the stock modlib path for arbitrary ET_REL modules is proven (G0 for
 * target modules); the installer control surface works now.
 */
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_manager_native_probe.h"
#include "canopus_installer_bundle.h"
#include "canopus_runtime.h"
#include "canopus_veneer.h" /* canopus_fw_register_driver / canopus_fw_unregister_driver */
#include "sha256.h"

#define CANOPUS_SUP_DEVICE_PATH "/dev/canopus"
#define CANOPUS_SUP_INBOX_ROOT "/data/canopus/inbox/"
#define CANOPUS_SUP_RECEIPT_SUFFIX ".cmi"
#define CANOPUS_SUP_MODULE_SUFFIX ".ko"
#define CANOPUS_SUP_PATH_MAX 224u
#define CANOPUS_SUP_READ_CHUNK 512u
#define CANOPUS_SUP_NUTTX_O_RDONLY 1
#define CANOPUS_SUP_NUTTX_OPEN UINT32_C(0x0C1C15B1)
#define CANOPUS_SUP_NUTTX_CLOSE UINT32_C(0x0C1AAB71)
#define CANOPUS_SUP_NUTTX_READ UINT32_C(0x0C1C1E25)
#define CANOPUS_SUP_NUTTX_ERRNO_LOCATION UINT32_C(0x0C1D5145)
#define CANOPUS_SUP_INSMOD UINT32_C(0x0C1EE091)
#define CANOPUS_SUP_RMMOD UINT32_C(0x0C1EE09D)
#define CANOPUS_SUP_MODHANDLE UINT32_C(0x0C1EE0A9)
#define CANOPUS_SUP_WATCHFACE_DELETE UINT32_C(0x0CA7D2E1)
#define CANOPUS_SUP_TARGET_ID "xiaomi-band-10-pro-3.101.030"
#define CANOPUS_SUP_DEVICE_MODE 438u /* 0666 */
#define CANOPUS_SUP_FOPS_WORDS 12u   /* matches the stock file_operations table */

/* CAN-P2-002: the fops table is typed from the recovered layout instead of a
 * bare uint32_t[12]. The veneer's `file_operations` carries the exact device
 * byte layout (open/close/read/write at +0x0..+0xc, ioctl at +0x14, 0x30
 * total); a compile-time size assertion locks it to the 12-word table the
 * driver expects. */
static file_operations s_fops;

CANOPUS_STATIC_ASSERT(sizeof(file_operations) == 0x30u,
                      "file_operations must match the 12-word stock table");
CANOPUS_STATIC_ASSERT(CANOPUS_SUP_FOPS_WORDS * 4u == sizeof(file_operations),
                      "fops word count must equal the typed layout");

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
    return canopus_supervisor_device_read(canopus_supervisor_get(), buffer, count);
}

static int32_t sup_control_write(void *filep, const void *buffer, uint32_t count)
{
    (void)filep;
    return canopus_supervisor_device_write(canopus_supervisor_get(), buffer, count);
}

static int sup_register_device(void *cookie)
{
    (void)cookie;
    s_fops.open = (void *)(uintptr_t)&sup_control_open;
    s_fops.close = (void *)(uintptr_t)&sup_control_close;
    s_fops.read = (void *)(uintptr_t)&sup_control_read;
    s_fops.write = (void *)(uintptr_t)&sup_control_write;
    return canopus_fw_register_driver(CANOPUS_SUP_DEVICE_PATH,
                                     (const void *)&s_fops,
                                     CANOPUS_SUP_DEVICE_MODE,
                                     (void *)0);
}

static int sup_unregister_device(void *cookie)
{
    (void)cookie;
    return canopus_fw_unregister_driver(CANOPUS_SUP_DEVICE_PATH);
}

static const uint8_t s_installer_public_key[32] = {
    0x85, 0xcb, 0x6b, 0x0d, 0xf8, 0x42, 0xa7, 0xaf,
    0xf0, 0xb8, 0x1a, 0xa9, 0xd4, 0x8a, 0x5c, 0x25,
    0x2a, 0x8a, 0x11, 0xa0, 0x61, 0x88, 0xd2, 0xa4,
    0x28, 0xe9, 0x2a, 0x77, 0x72, 0x5d, 0xa5, 0x55,
};

static const uint8_t s_firmware_sha256[32] = {
    0xf7, 0x01, 0xa8, 0x4f, 0xfc, 0xaf, 0xa6, 0x7f,
    0x4d, 0x46, 0x03, 0xad, 0x8c, 0xd6, 0x6a, 0x11,
    0xe5, 0x44, 0x2f, 0x27, 0x14, 0x0f, 0x5a, 0xf0,
    0x98, 0x2e, 0x09, 0x75, 0xdc, 0xcd, 0x22, 0x5b,
};

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

static int sup_verify_package_at(
    const char *token, struct canopus_install_receipt_v1 *receipt,
    const char *module_path, uint32_t *diagnostic)
{
    uint8_t digest[CANOPUS_INSTALL_DIGEST_SIZE];
    char receipt_path[CANOPUS_SUP_PATH_MAX];

    if (diagnostic == 0 || receipt == 0 || module_path == 0 ||
        sup_make_path(receipt_path, token, CANOPUS_SUP_RECEIPT_SUFFIX) != 0) {
        if (diagnostic != 0) *diagnostic = CANOPUS_SUP_ERR_STAGE_PATH;
        return -1;
    }
    if (sup_read_exact(receipt_path, receipt, sizeof(*receipt), diagnostic) != 0) {
        return -1;
    }
    if (canopus_install_receipt_validate(receipt, CANOPUS_SUP_TARGET_ID,
                                         s_firmware_sha256,
                                         s_installer_public_key) != 0 ||
        !sup_module_id_equal(receipt->module_id, token)) {
        *diagnostic = CANOPUS_SUP_ERR_STAGE_SIGNATURE;
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

static int sup_load_module(void *cookie, uint32_t index,
                           const char *name, uint32_t lifecycle_class)
{
    typedef void *(*insmod_fn)(const char *, const char *);
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_install_receipt_v1 receipt;
    insmod_fn insmod = (insmod_fn)(uintptr_t)CANOPUS_SUP_INSMOD;
    char path[CANOPUS_SUP_PATH_MAX];
    const char *module_name;
    void *handle;

    (void)cookie;
    if (sup == 0 || index >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->modules[index].state == 0u) return -1;
    module_name = name != 0 ? name : (const char *)sup->modules[index].module_id;
    /* The inbox artifact is revalidated immediately before every insmod. A
     * subsequent target pack may promote this to a proven atomic fd loader;
     * this firmware's verified public modlib API accepts paths only. */
    if (!sup_module_id_equal(sup->modules[index].module_id, module_name) ||
        sup_make_path(path, module_name, CANOPUS_SUP_MODULE_SUFFIX) != 0 ||
        sup_verify_package_at(module_name, &receipt, path,
                              &sup->error_code) != 0 ||
        receipt.lifecycle_class != sup->modules[index].lifecycle_class ||
        receipt.lifecycle_class != lifecycle_class ||
        receipt.module_version != sup->modules[index].version) {
        return -1;
    }
    handle = insmod(path, module_name);
    if (handle == 0) return -1;
    return lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE
               ? CANOPUS_STATE_ACTIVE : CANOPUS_STATE_BOOT_RESIDENT;
}

static int sup_unload_module(void *cookie, uint32_t index)
{
    typedef void *(*modhandle_fn)(const char *);
    typedef int (*rmmod_fn)(void *);
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    modhandle_fn modhandle = (modhandle_fn)(uintptr_t)CANOPUS_SUP_MODHANDLE;
    rmmod_fn rmmod = (rmmod_fn)(uintptr_t)CANOPUS_SUP_RMMOD;
    void *handle;

    (void)cookie;
    if (sup == 0 || index >= CANOPUS_SUP_MODULE_SLOTS ||
        sup->modules[index].state == 0u) return -1;
    handle = modhandle((const char *)sup->modules[index].module_id);
    return handle != 0 && rmmod(handle) >= 0 ? 0 : -1;
}

static int sup_stage_package(void *cookie, const char *token)
{
    typedef int (*watchface_delete_fn)(const char *);
    watchface_delete_fn delete_watchface =
        (watchface_delete_fn)(uintptr_t)CANOPUS_SUP_WATCHFACE_DELETE;
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_install_receipt_v1 receipt;
    char module_path[CANOPUS_SUP_PATH_MAX];
    uint32_t i;
    uint32_t diagnostic;
    int slot;

    (void)cookie;
    /* Legacy payload-free INSTALL remains the Manager bootstrap. */
    if (token == 0) return canopus_manager_native_install();
    if (sup == 0) return -1;
    /* Reject a duplicate before registering a second slot. */
    for (i = 0u; i < CANOPUS_SUP_MODULE_SLOTS; i++) {
        if (sup->modules[i].state != 0u &&
            sup_module_id_equal(sup->modules[i].module_id, token)) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE_DUPLICATE;
            return -1;
        }
    }
    /* Copy succeeds only after the signature and artifact are verified. Keep
     * the inbox files in place: Lua is the writer and later ENABLE resolves the
     * verified artifact through this stable path. */
    if (sup_make_path(module_path, token, CANOPUS_SUP_MODULE_SUFFIX) != 0) {
        sup->error_code = CANOPUS_SUP_ERR_STAGE_PATH;
        return -1;
    }
    if (sup_verify_package_at(token, &receipt, module_path, &diagnostic) != 0) {
        sup->error_code = diagnostic;
        return -1;
    }
    slot = canopus_supervisor_add_module(
        sup, receipt.lifecycle_class, receipt.module_version, 1u,
        (const char *)receipt.module_id);
    if (slot < 0) {
        if (sup->error_code == CANOPUS_SUP_ERR_NONE) {
            sup->error_code = CANOPUS_SUP_ERR_STAGE_REGISTER;
        }
        return -1;
    }
    if (canopus_manager_native_notify_module_installed() < 0) {
        /* Registration has already committed a verified, disabled module.
         * Notification is a delivery side effect: preserve the installer page
         * for manual cleanup, but never roll back a valid installation merely
         * because the stock notification service rejects this message. */
        return 0;
    }
    /* The firmware watchface manager switches away before deleting an active
     * non-last watchface. Failure is non-fatal: installation remains complete
     * and the installer page can show diagnostics for manual removal. */
    (void)delete_watchface(token);
    return 0;
}

const struct canopus_sup_platform_v1 canopus_sup_platform = {
    sup_register_device,
    sup_unregister_device,
    sup_load_module,
    sup_unload_module,
    sup_stage_package,
    0, /* remove_artifact: inbox module remains for disabled/retry flow */
    0, /* deactivate: device teardown happens inside unload (G0 pending) */
    0, /* stop */
};
