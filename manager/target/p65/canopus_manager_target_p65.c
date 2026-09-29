/* Experimental exact-P65 native Manager. Resident until reboot.
 * Firmware signatures are isolated here; shared semantic UI/CPC2 is unchanged.
 * Layout: [Canopus title] -> [Overview / Modules] -> [Details / confirmation].
 */
#include <stddef.h>
#include "canopus_p65_abi.h"
#include "canopus_p65_notification.h"
#include "canopus_manager_target.h"
#include "canopus_manager_p65_backend.h"
#include "canopus_supervisor_platform.h"
#include "canopus_supervisor.h"
#include "canopus_memory.h"
#include "canopus_veneer.h"

#define CALL(address, type) ((type)(uintptr_t)(address))
typedef void *(*create_fn)(void *);
typedef void (*text_fn)(void *, const char *);
typedef void (*one_fn)(void *);
typedef void (*size_fn)(void *, int32_t, int32_t);
typedef void (*align_fn)(void *, uint32_t, int32_t, int32_t);
typedef void (*event_fn)(void *, void (*)(void *), uint32_t, void *);

/* P65 uses the +100 UI-destruction slot, as shown at C22126C.
 * Do not use the Band10 +96 UI-destruction layout. */
struct p65_page {
    void *parent;
    uint8_t pad4[12];
    const char *name;
    uint16_t page_id, app_id, flags;
    uint8_t pad26[2];
    int32_t deadline, priority;
    uint32_t refs;
    uint8_t state, layer, kind, pad43;
    void *activity, *root;
    int (*signal)(struct p65_page *, uint32_t, void *);
    void *default56, *user, *prev, *next, *runtime_parent;
    int (*create)(struct p65_page *, void *, void *);
    int (*resume)(struct p65_page *);
    void *foreground, *intermediate;
    int (*pause)(struct p65_page *);
    int (*stop)(struct p65_page *);
    int (*destroy_ui)(struct p65_page *);
    void *extension[4];
};
struct p65_app {
    void *prev, *next;
    const char *package, *icon;
    uint16_t app_id;
    uint8_t flags, reserved;
    const char *owned20, *owned24;
    const char *(*name)(void);
    uint8_t pad32[16];
    void *pages;
    uint8_t pad52[8];
    uint8_t hidden, tail[3];
};
_Static_assert(sizeof(struct p65_app) == 64, "P65 app descriptor");
_Static_assert(offsetof(struct p65_page, create) == 76, "P65 page create");
_Static_assert(offsetof(struct p65_page, destroy_ui) == 100, "P65 UI destroy");

volatile struct canopus_manager_target_record canopus_manager_target_record = {
    .magic = CANOPUS_MANAGER_TARGET_MAGIC,
    .build_id = CANOPUS_MANAGER_TARGET_BUILD_ID,
};
static struct canopus_manager_p65_backend_v1 backend;
static uint8_t installed;
static uint8_t page_live;
static const char title[] = "Canopus";

static int32_t list_create(void *cookie, void *parent, const void *descriptor,
                            void **out)
{
    (void)cookie; (void)descriptor;
    *out = CALL(P65_LIST_CREATE, create_fn)(parent);
    if (!*out) return CANOPUS_UI_ERR_BACKEND;
    CALL(P65_LIST_SET_TITLE, text_fn)(*out, title);
    return 0;
}
static int32_t list_configure(void *cookie, void *list,
    const struct canopus_p65_lvx_callback_table_v1 *table)
{
    typedef void (*fn)(void *, const void *);
    (void)cookie;
    CALL(P65_LIST_CONFIGURE, fn)(list, table);
    return 0;
}
static int32_t list_finalize(void *cookie, void *list)
{
    (void)cookie;
    CALL(P65_LIST_FINALIZE, one_fn)(list);
    return 0;
}
static void list_refresh(void *cookie, void *list)
{
    typedef void (*fn)(void *, uint32_t, uint32_t);
    (void)cookie;
    CALL(P65_LIST_REFRESH, fn)(list, 0, 0);
}
static void row_remove_event(void *cookie, void *list, void *row)
{
    typedef void (*fn)(void *, void (*)(void *));
    (void)cookie; (void)list;
    CALL(P65_EVENT_REMOVE, fn)(row, canopus_p65_ui_row_click_event);
}
static void row_update(void *cookie, void *list, void *row,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    void **words = row;
    (void)cookie; (void)list;
    /* Stock listview's child label slots recovered in C764638/C7644A0. */
    CALL(P65_LABEL_TEXT, text_fn)(words[14], view->primary ? view->primary : "");
    CALL(P65_LABEL_TEXT, text_fn)(words[15], view->secondary ? view->secondary : "");
    row_remove_event(cookie, list, row);
    CALL(P65_EVENT_ADD, event_fn)(row, canopus_p65_ui_row_click_event, 7, binding);
}
static void *row_create(void *cookie, void *list,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    typedef void *(*class_fn)(const void *, void *);
    typedef void (*flag_fn)(void *, uint32_t);
    void **row = CALL(P65_ROW_CREATE, class_fn)(
        (const void *)(uintptr_t)P65_ROW_CLASS, list);
    if (!row) return 0;
    row[14] = CALL(P65_LABEL_CREATE, create_fn)(row);
    row[15] = CALL(P65_LABEL_CREATE, create_fn)(row);
    if (!row[14] || !row[15]) return 0;
    CALL(P65_OBJECT_SIZE, size_fn)(row, 420, 116);
    CALL(P65_OBJECT_SIZE, size_fn)(row[14], 370, 45);
    CALL(P65_OBJECT_SIZE, size_fn)(row[15], 370, 36);
    CALL(P65_OBJECT_ALIGN, align_fn)(row[14], 1, 24, 14);
    CALL(P65_OBJECT_ALIGN, align_fn)(row[15], 1, 24, 64);
    CALL(P65_OBJECT_ADD_FLAG, flag_fn)(row, 16);
    row_update(cookie, list, row, view, binding);
    return row;
}
static const struct canopus_p65_lvx_list_ops_v1 list_ops = {
    sizeof(list_ops), CANOPUS_P65_LVX_ABI_MAJOR, CANOPUS_P65_LVX_ABI_MINOR,
    list_create, list_configure, list_finalize, list_refresh,
    row_remove_event, row_create, row_update,
};
static int page_signal(struct p65_page *page, uint32_t event, void *data)
{
    (void)page; (void)event; (void)data;
    return 0;
}
static int page_create(struct p65_page *page, void *root, void *data)
{
    int rc;
    (void)data;
    if (page_live) return -16;
    /* Firmware owns root deletion; backend storage is boot-resident. */
    page->root = root;
    page_live = 1;
    canopus_manager_target_record.create_count++;
    canopus_manager_target_record.root_object = (uintptr_t)root;
    (void)canopus_supervisor_restore_after_boot();
    rc = canopus_manager_p65_backend_init(&backend, canopus_supervisor_get(),
        &list_ops, 0, root, title, CANOPUS_TARGET_ID,
        CANOPUS_TARGET_FIRMWARE_VERSION, CANOPUS_TARGET_FIRMWARE_BUILD, 1);
    return rc;
}
static int page_resume(struct p65_page *page)
{
    (void)page;
    canopus_manager_target_record.resume_count++;
    if (!backend.initialized) return -1;
    if (canopus_manager_p65_backend_refresh(&backend) != 0) return -1;
    return canopus_manager_native_render(&backend.native);
}
static int page_pause(struct p65_page *page)
{
    (void)page;
    canopus_manager_target_record.pause_count++;
    return 0;
}
static int page_destroy(struct p65_page *page)
{
    (void)page;
    /* Called immediately before firmware deletes root and descendants.
     * Resident binding memory is not freed; block dispatch before teardown. */
    backend.native.ui.has_committed = 0;
    backend.native.ui.event_handler = 0;
    (void)canopus_manager_p65_backend_close(&backend);
    page_live = 0;
    canopus_manager_target_record.destroy_count++;
    canopus_manager_target_record.root_object = 0;
    return 0;
}
static const char *display_name(void) { return title; }
static struct p65_app app = {
    .package = "com.canopus.manager",
    .icon = "/data/canopus/manager_icon.bin",
    .app_id = CANOPUS_MANAGER_TARGET_APP_ID,
    .name = display_name,
};
static struct p65_page page = {
    .name = "main", .app_id = CANOPUS_MANAGER_TARGET_APP_ID,
    .signal = page_signal, .create = page_create, .resume = page_resume,
    .pause = page_pause, .destroy_ui = page_destroy,
};
static struct p65_page *pages[] = { &page };

int canopus_manager_native_install(void)
{
    typedef void (*install_fn)(void *, void *, uint32_t);
    typedef void *(*lookup_fn)(const char *);
    typedef void (*refresh_fn)(void);
    int rc = canopus_identity_guard();
    canopus_manager_target_record.identity_result = rc;
    if (rc != 0) return rc;
    if (installed) return 0;
    if (CALL(P65_APP_LOOKUP, lookup_fn)(app.package) != 0) return -17;
    CALL(P65_APP_INSTALL, install_fn)(&app, pages, 1);
    if (CALL(P65_APP_LOOKUP, lookup_fn)(app.package) == 0) {
        canopus_manager_target_record.app_install_result = -1;
        return -1;
    }
    installed = 1;
    CALL(P65_LAUNCHER_REFRESH, refresh_fn)();
    canopus_manager_target_record.app_install_result = 0;
    return 0;
}
int canopus_manager_native_notify_module_installed(void)
{
    typedef int (*notify_fn)(const struct canopus_p65_notification_v1 *);
    struct canopus_p65_notification_v1 message;
    int result;
    if (canopus_identity_guard() != 0) return -1;
    canopus_p65_notification_init(&message, UINT64_C(0x43414E500001),
        (uint32_t)(uintptr_t)"Module installed",
        (uint32_t)(uintptr_t)title,
        (uint32_t)(uintptr_t)"Open Canopus Manager to review and enable it.",
        (uint32_t)(uintptr_t)"/data/canopus/manager_icon.bin");
    result = CALL(P65_NOTIFICATION_INSERT, notify_fn)(&message);
    canopus_manager_target_record.notification_result = result;
    /* Exact insertion return semantics are inconsistent across flags82
     * branches. Preserve the raw result, do not fail a committed install. */
    return result < 0 ? result : 0;
}
void canopus_manager_target_render_diagnostics(uint8_t out[36])
{
    uint32_t words[9] = {
        canopus_manager_target_record.magic,
        (uint32_t)canopus_manager_target_record.identity_result,
        (uint32_t)canopus_manager_target_record.app_install_result,
        (uint32_t)canopus_manager_target_record.launcher_add_result,
        (uint32_t)canopus_manager_target_record.notification_result,
        canopus_manager_target_record.create_count,
        canopus_manager_target_record.resume_count,
        canopus_manager_target_record.pause_count,
        canopus_manager_target_record.destroy_count,
    };
    uint32_t i;
    for (i = 0; i < 9; i++) {
        out[4*i] = (uint8_t)words[i];
        out[4*i+1] = (uint8_t)(words[i] >> 8);
        out[4*i+2] = (uint8_t)(words[i] >> 16);
        out[4*i+3] = (uint8_t)(words[i] >> 24);
    }
}
