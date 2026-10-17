#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"
#include "canopus_installer_bundle.h"
#include "canopus_memory.h"
#include "canopus_services.h"

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

static void upload_abort(struct canopus_supervisor_v1 *sup)
{
    char path[96];
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
    uint32_t version, lifecycle;

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
    if (pf->file_rename(sup->platform_cookie, path, next) != 0 ||
        pf->stage_verify(sup->platform_cookie, sup->upload.id, &version, &lifecycle) != 0)
        goto failed;
    return CANOPUS_RESULT_COMPLETED;

failed:
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
