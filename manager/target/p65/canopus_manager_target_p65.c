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
/* P65 title creation takes a mode, event callback and stack-passed cookie.
 * Calling it as text_fn leaks unspecified registers/stack into LVGL. */
typedef void *(*title_fn)(void *, const char *, uint32_t,
                          void (*)(void *), void *);
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
static uint8_t page_live;
static const char title[] = "Canopus";

static int32_t list_create(void *cookie, void *parent, const void *descriptor,
                            void **out)
{
    (void)cookie; (void)descriptor;
    *out = CALL(P65_LIST_CREATE, create_fn)(parent);
    if (!*out) return CANOPUS_UI_ERR_BACKEND;
    if (!CALL(P65_LIST_CREATE_TITLE, title_fn)(*out, title, 0, 0, 0))
        return CANOPUS_UI_ERR_BACKEND;
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
    (void)cookie;
    CALL(P65_LIST_REFRESH, one_fn)(list);
}
static void row_remove_event(void *cookie, void *list, void *row)
{
    typedef void (*fn)(void *, void (*)(void *));
    (void)cookie; (void)list;
    CALL(P65_EVENT_REMOVE, fn)(row, canopus_p65_ui_row_click_event);
}
/* Use the same LVX settings cards as the official Manager, not Calendar's
 * all_list rows (their secondary style is 40 px and has no action affordance). */
typedef void (*row_content_fn)(void *, const void *, const char *,
                              const char *, int32_t, uint32_t, uint32_t);
typedef void (*row_update_fn)(void *, const void *, const char *,
                             const char *, int32_t, uint32_t);
typedef void (*flag_fn)(void *, uint32_t);
static int plain_row(const struct canopus_p65_ui_row_view_v1 *view)
{
    return view->node->type == CANOPUS_UI_NODE_SECTION ||
           view->node->type == CANOPUS_UI_NODE_TEXT;
}
static void bind_row_event(void *row,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    /* Scrolling belongs to the list. Only enabled action rows receive clicks. */
    CALL(P65_OBJECT_CLEAR_FLAG, flag_fn)(row, 16);
    row_remove_event(0, 0, row);
    if (view->event_id != 0u) {
        CALL(P65_OBJECT_ADD_FLAG, flag_fn)(row, 2);
        CALL(P65_EVENT_ADD, event_fn)(row, canopus_p65_ui_row_click_event, 7, binding);
    } else {
        CALL(P65_OBJECT_CLEAR_FLAG, flag_fn)(row, 2);
    }
}
static void row_wrap_secondary(void *row)
{
    /* LVX owns child pointers at +92/+96, not Calendar's +56/+60.
     * Keep the stock fonts, colors, rounded background and padding. */
    void **words = row;
    void *secondary = words[24];
    if (secondary != 0) {
        CALL(P65_LABEL_LONG_MODE, flag_fn)(secondary, 0);
        CALL(P65_OBJECT_SIZE, size_fn)(secondary, (int32_t)(uintptr_t)words[25],
                                      (int32_t)UINT32_C(0x200007D1));
    }
}
static void row_update(void *cookie, void *list, void *row,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    (void)cookie; (void)list;
    if (plain_row(view)) {
        CALL(P65_LABEL_TEXT, text_fn)(row, view->primary ? view->primary : "");
    } else {
        CALL(P65_SETTINGS_ROW_UPDATE, row_update_fn)(row, 0,
            view->primary ? view->primary : "", view->secondary ? view->secondary : "",
            0, view->node->type != CANOPUS_UI_NODE_SWITCH_ROW ||
               (view->node->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u);
        row_wrap_secondary(row);
    }
    bind_row_event(row, view, binding);
}
static void *row_create(void *cookie, void *list,
    const struct canopus_p65_ui_row_view_v1 *view,
    struct canopus_p65_ui_event_binding_v1 *binding)
{
    void *row;
    (void)cookie;
    if (plain_row(view)) {
        typedef void (*style_fn)(void *, const void *, uint32_t, uint32_t);
        row = CALL(P65_LABEL_CREATE, create_fn)(list);
        if (!row) return 0;
        CALL(P65_TEXT_STYLE_APPLY, style_fn)(row,
            (const void *)(uintptr_t)P65_PRIMARY_TEXT_STYLE, 255, 0);
        CALL(P65_OBJECT_SIZE, size_fn)(row, 416, 40);
        CALL(P65_LABEL_TEXT, text_fn)(row, view->primary ? view->primary : "");
    } else {
        uint32_t trailing = view->event_id != 0u ? 3u : 0u;
        row = CALL(P65_SETTINGS_ROW_CREATE, create_fn)(list);
        if (!row) return 0;
        CALL(P65_SETTINGS_ROW_CONTENT, row_content_fn)(row, 0,
            view->primary ? view->primary : "", view->secondary ? view->secondary : "",
            0, (view->node->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u, trailing);
    }
    /* Upstream populates once, then updates selection/state on every render. */
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
    if (!page || !root) return CANOPUS_UI_ERR_ARGUMENT;
    if (page_live) return -16;
    /* Firmware owns root deletion; backend storage is boot-resident. */
    page->root = root;
    page_live = 1;
    canopus_manager_target_record.create_count++;
    canopus_manager_target_record.root_object = (uintptr_t)root;
    rc = canopus_manager_p65_backend_init(&backend, canopus_supervisor_get(),
        &list_ops, 0, root, title, CANOPUS_TARGET_ID,
        CANOPUS_TARGET_FIRMWARE_VERSION, CANOPUS_TARGET_FIRMWARE_BUILD, 1);
    if (rc != 0) {
        backend.native.ui.has_committed = 0;
        backend.native.ui.event_handler = 0;
        page_live = 0;
    }
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

/* Internal package registration and launcher publication are separate.
 * Bound traversal also makes a corrupt/cyclic firmware list fail closed. */
static int launcher_contains_manager(void)
{
    typedef void *(*list_fn)(uint32_t);
    typedef void *(*first_fn)(void *);
    typedef void *(*next_fn)(void *, void *);
    void *list = CALL(P65_LAUNCHER_LIST, list_fn)(0);
    void *item;
    uint32_t count;
    if (!list) return -1;
    item = CALL(P65_LIST_FIRST, first_fn)(list);
    for (count = 0; item && count < 512; count++) {
        if (*(const uint16_t *)item == app.app_id) return 1;
        item = CALL(P65_LIST_NEXT, next_fn)(list, item);
    }
    return item ? -1 : 0;
}
int canopus_manager_native_install(void)
{
    typedef void (*install_fn)(void *, void *, uint32_t);
    typedef struct p65_app *(*lookup_fn)(const char *);
    typedef void (*add_fn)(uint16_t);
    struct p65_app *registered;
    int rc;
    canopus_manager_target_record.launcher_add_result = -1;
    if (!CANOPUS_P65_SESSION_ENABLED) {
        canopus_manager_target_record.app_install_result =
            CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED;
        return CANOPUS_P65_ERR_DEPLOYMENT_BLOCKED;
    }
    rc = canopus_identity_guard();
    canopus_manager_target_record.identity_result = rc;
    if (rc != 0) return rc;
    registered = CALL(P65_APP_LOOKUP, lookup_fn)(app.package);
    if (!registered) {
        CALL(P65_APP_INSTALL, install_fn)(&app, pages, 1);
        registered = CALL(P65_APP_LOOKUP, lookup_fn)(app.package);
    }
    /* Permit retry after publication failure, but never adopt another
     * resident Manager's callbacks (for example an older loaded binary). */
    rc = !registered ? -1 :
        (registered->app_id != app.app_id || registered->name != display_name
         ? -17 : 0);
    canopus_manager_target_record.app_install_result = rc;
    if (rc != 0) return rc;
    rc = launcher_contains_manager();
    if (rc == 0) {
        CALL(P65_LAUNCHER_ADD, add_fn)(app.app_id);
        rc = launcher_contains_manager();
    }
    canopus_manager_target_record.launcher_add_result = rc == 1 ? 0 : -1;
    return canopus_manager_target_record.launcher_add_result;
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
