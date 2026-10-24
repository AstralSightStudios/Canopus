#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_installer_bundle.h"
#include "canopus_memory.h"
#include "canopus_services.h"
#include "sha256.h"

static uint32_t upload_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static void upload_path(char *out, const char *id, const char *suffix)
{
    const char *root = "/data/canopus/inbox/";
    while (*root) *out++ = *root++;
    while (*id) *out++ = *id++;
    while ((*out++ = *suffix++) != '\0') {}
}

static int upload_write(struct canopus_supervisor_v1 *sup, int fd,
                        const uint8_t *data, uint32_t len)
{
    while (len != 0u) {
        int32_t n = sup->platform->file_write(sup->platform_cookie, fd, data, len);
        if (n <= 0 || (uint32_t)n > len) return -1;
        data += n;
        len -= (uint32_t)n;
    }
    return 0;
}

static void resource_path(char *out, const struct canopus_supervisor_v1 *sup,
                          const char *name, const char *suffix)
{
    const char *root = "/data/canopus/m.";
    const char *id = sup->upload.id;
    while (*root) *out++ = *root++;
    while (*id) *out++ = *id++;
    if (name != 0) {
        *out++ = '/';
        while (*name) *out++ = *name++;
    }
    while ((*out++ = *suffix++) != '\0') {}
}

static void resource_abort(struct canopus_supervisor_v1 *sup)
{
    char path[CANOPUS_FS_PATH_MAX];
    if (!sup->resource_upload.active) return;
    if (sup->resource_upload.fd >= 0)
        (void)sup->platform->file_close(sup->platform_cookie, sup->resource_upload.fd);
    resource_path(path, sup, sup->resource_upload.name, ".upload");
    (void)sup->platform->file_remove(sup->platform_cookie, path);
    sup->resource_upload.active = 0u;
}

static int resource_name_ok(const uint8_t *name, uint32_t len)
{
    uint32_t i;
    if (len == 0u || len > CANOPUS_FS_NAME_MAX || name[0] == '.') return 0;
    for (i = 0u; i < len; i++) {
        uint8_t c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'))
            return 0;
    }
    return 1;
}

/* Verify the staged bytes, including exact EOF, before publishing the file. */
static int resource_verify(struct canopus_supervisor_v1 *sup, const char *path)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    SHA256_CTX sha;
    /* One publish-time readback per file; larger blocks keep the flash syscall
     * count down (bounded by this task's stack). */
    uint8_t chunk[1024], digest[32];
    uint32_t left = sup->resource_upload.size, i;
    int fd = pf->file_open(sup->platform_cookie, path, CANOPUS_FS_READ);
    int rc = -1;
    if (fd < 0) return -1;
    sha256_init(&sha);
    while (left != 0u) {
        uint32_t want = left < sizeof(chunk) ? left : sizeof(chunk);
        int32_t n = pf->file_read(sup->platform_cookie, fd, chunk, want);
        if (n <= 0 || (uint32_t)n > want) goto done;
        sha256_update(&sha, chunk, (uint32_t)n);
        left -= (uint32_t)n;
    }
    if (pf->file_read(sup->platform_cookie, fd, chunk, 1u) != 0) goto done;
    sha256_final(&sha, digest);
    for (i = 0u; i < sizeof(digest); i++) {
        if (digest[i] != sup->resource_upload.sha256[i]) goto done;
    }
    rc = 0;
done:
    if (pf->file_close(sup->platform_cookie, fd) != 0) rc = -1;
    return rc;
}

static uint32_t resource_upload(struct canopus_supervisor_v1 *sup,
                                uint32_t command, const uint8_t *payload,
                                uint32_t len)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    char path[CANOPUS_FS_PATH_MAX], next[CANOPUS_FS_PATH_MAX];
    struct canopus_fs_stat st;
    int fd, rc;
    uint32_t name_len;

    if (!sup->upload.resources_ready) goto disallowed;
    if (pf->file_read == 0 || pf->file_stat == 0 || pf->dir_mkdir == 0)
        goto disallowed;
    if (command == CANOPUS_CMD_STAGE_RES_BEGIN) {
        if (sup->resource_upload.active || len < 37u) goto disallowed;
        name_len = payload[0];
        if (len != 37u + name_len || !resource_name_ok(payload + 1u, name_len))
            goto disallowed;
        canopus_memcpy(sup->resource_upload.name, payload + 1u, name_len);
        sup->resource_upload.name[name_len] = '\0';
        sup->resource_upload.size = upload_u32(payload + 1u + name_len);
        canopus_memcpy(sup->resource_upload.sha256, payload + 5u + name_len, 32u);
        resource_path(path, sup, 0, "");
        rc = pf->dir_mkdir(sup->platform_cookie, path);
        if (rc != 0 && (rc != CANOPUS_SVC_ERR_EXIST ||
            pf->file_stat(sup->platform_cookie, path, &st) != 0 || !st.is_dir))
            goto failed;
        resource_path(path, sup, sup->resource_upload.name, ".upload");
        fd = pf->file_open(sup->platform_cookie, path, CANOPUS_FS_WRITE);
        if (fd < 0) goto failed;
        sup->resource_upload.fd = fd;
        sup->resource_upload.written = 0u;
        sup->resource_upload.active = 1u;
        return CANOPUS_RESULT_COMPLETED;
    }
    if (!sup->resource_upload.active) goto disallowed;
    if (command == CANOPUS_CMD_STAGE_RES_WRITE) {
        if (len <= 4u || len > CANOPUS_PROTO_MAX_PAYLOAD ||
            upload_u32(payload) != sup->resource_upload.written ||
            len - 4u > sup->resource_upload.size - sup->resource_upload.written)
            goto disallowed;
        if (upload_write(sup, sup->resource_upload.fd, payload + 4u, len - 4u) != 0)
            goto failed;
        sup->resource_upload.written += len - 4u;
        return CANOPUS_RESULT_COMPLETED;
    }
    if (command != CANOPUS_CMD_STAGE_RES_FINISH || len != 0u ||
        sup->resource_upload.written != sup->resource_upload.size)
        goto disallowed;
    fd = sup->resource_upload.fd;
    sup->resource_upload.fd = -1;
    if (pf->file_close(sup->platform_cookie, fd) != 0) goto failed;
    resource_path(path, sup, sup->resource_upload.name, ".upload");
    if (resource_verify(sup, path) != 0) goto failed;
    resource_path(next, sup, sup->resource_upload.name, "");
    (void)pf->file_remove(sup->platform_cookie, next);
    if (pf->file_rename(sup->platform_cookie, path, next) != 0) goto failed;
    sup->resource_upload.active = 0u;
    return CANOPUS_RESULT_COMPLETED;

disallowed:
    resource_abort(sup);
    return CANOPUS_RESULT_DISALLOWED;
failed:
    resource_abort(sup);
    return CANOPUS_RESULT_FAILED;
}

static void upload_abort(struct canopus_supervisor_v1 *sup)
{
    char path[96];
    resource_abort(sup);
    sup->upload.resources_ready = 0u;
    if (!sup->upload.active) return;
    (void)sup->platform->file_close(sup->platform_cookie, sup->upload.fd);
    upload_path(path, sup->upload.id, ".ko.upload");
    (void)sup->platform->file_remove(sup->platform_cookie, path);
    sup->upload.active = 0u;
}

uint32_t canopus_supervisor_upload(struct canopus_supervisor_v1 *sup,
                                  uint32_t command, const uint8_t *payload,
                                  uint32_t len)
{
    const struct canopus_sup_platform_v1 *pf = sup->platform;
    char path[96], next[96];
    int fd;

    if (pf == 0 || pf->file_open == 0 || pf->file_write == 0 ||
        pf->file_close == 0 || pf->file_remove == 0 || pf->file_rename == 0 ||
        pf->validate_receipt == 0 || pf->stage_verify == 0) {
        return CANOPUS_RESULT_DISALLOWED;
    }
    if (command == CANOPUS_CMD_STAGE_ABORT) {
        if (len != 0u) return CANOPUS_RESULT_DISALLOWED;
        upload_abort(sup);
        return CANOPUS_RESULT_COMPLETED;
    }
    if (command >= CANOPUS_CMD_STAGE_RES_BEGIN &&
        command <= CANOPUS_CMD_STAGE_RES_FINISH)
        return resource_upload(sup, command, payload, len);
    if (command == CANOPUS_CMD_STAGE_BEGIN) {
        struct canopus_install_receipt_v1 receipt;
        if (len != sizeof(receipt)) return CANOPUS_RESULT_DISALLOWED;
        canopus_memcpy(&receipt, payload, sizeof(receipt));
        if (pf->validate_receipt(sup->platform_cookie, &receipt) != 0)
            return CANOPUS_RESULT_DISALLOWED;
        upload_abort(sup);
        canopus_memcpy(sup->upload.id, receipt.module_id, sizeof(sup->upload.id));
        canopus_memcpy(sup->upload.receipt, payload, sizeof(receipt));
        sup->upload.size = receipt.artifact_size;
        sup->upload.written = 0u;
        upload_path(path, sup->upload.id, ".ko.upload");
        fd = pf->file_open(sup->platform_cookie, path, CANOPUS_FS_WRITE);
        if (fd < 0) return CANOPUS_RESULT_FAILED;
        sup->upload.fd = fd;
        sup->upload.active = 1u;
        return CANOPUS_RESULT_COMPLETED;
    }
    if (!sup->upload.active) return CANOPUS_RESULT_DISALLOWED;
    if (command == CANOPUS_CMD_STAGE_WRITE) {
        if (len <= 4u || upload_u32(payload) != sup->upload.written ||
            len - 4u > sup->upload.size - sup->upload.written)
            return CANOPUS_RESULT_DISALLOWED;
        if (upload_write(sup, sup->upload.fd, payload + 4u, len - 4u) != 0) {
            upload_abort(sup);
            return CANOPUS_RESULT_FAILED;
        }
        sup->upload.written += len - 4u;
        return CANOPUS_RESULT_COMPLETED;
    }
    if (command != CANOPUS_CMD_STAGE_FINISH || len != 0u ||
        sup->upload.written != sup->upload.size) return CANOPUS_RESULT_DISALLOWED;
    fd = sup->upload.fd;
    sup->upload.active = 0u;
    upload_path(path, sup->upload.id, ".ko.upload");
    if (pf->file_close(sup->platform_cookie, fd) != 0) goto failed;
    upload_path(next, sup->upload.id, ".ko.next");
    (void)pf->file_remove(sup->platform_cookie, next);
    if (pf->file_rename(sup->platform_cookie, path, next) != 0) goto failed;
    upload_path(path, sup->upload.id, ".cmi.upload");
    fd = pf->file_open(sup->platform_cookie, path, CANOPUS_FS_WRITE);
    if (fd < 0) goto failed;
    {
        int rc = upload_write(sup, fd, sup->upload.receipt, sizeof(sup->upload.receipt));
        if (pf->file_close(sup->platform_cookie, fd) != 0 || rc != 0) goto failed;
    }
    upload_path(next, sup->upload.id, ".cmi.next");
    (void)pf->file_remove(sup->platform_cookie, next);
    if (pf->file_rename(sup->platform_cookie, path, next) != 0) goto failed;
    /* No artifact hash here: stage_verify runs exactly once before commit
     * (INSTALL) and again at module load. Re-reading the ELF at staging time
     * only duplicated the pre-commit check. */
    sup->upload.resources_ready = 1u;
    return CANOPUS_RESULT_COMPLETED;

failed:
    sup->upload.resources_ready = 0u;
    upload_path(path, sup->upload.id, ".ko.upload");
    (void)pf->file_remove(sup->platform_cookie, path);
    upload_path(path, sup->upload.id, ".cmi.upload");
    (void)pf->file_remove(sup->platform_cookie, path);
    upload_path(path, sup->upload.id, ".cmi.next");
    (void)pf->file_remove(sup->platform_cookie, path);
    upload_path(path, sup->upload.id, ".ko.next");
    (void)pf->file_remove(sup->platform_cookie, path);
    return CANOPUS_RESULT_FAILED;
}
