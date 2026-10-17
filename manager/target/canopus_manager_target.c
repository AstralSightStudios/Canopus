/*
 * LVGL v9 Manager target backend: Band 10 Pro by default, Band 11 with
 * -DCANOPUS_MGR_BAND11=1 (set by the family's `defines`). The two firmwares
 * differ in page/app/notification descriptor layouts, identity guard and a few
 * UI constants; everything else is shared.
 *
 * Target identity and firmware addresses are supplied by the selected target config. It is
 * resident-first: its constructor only records target identity. The supervisor
 * invokes the exported install entry from a /dev/canopus write made by miwear,
 * then install registers one writable page descriptor and one launcher record.
 * The page maps the portable Manager snapshot to stock LVX rows; firmware widget
 * pointers remain private to this target backend. Device testing must treat
 * reboot as the reliable recovery path until concurrent callback drain is proven.
 */
#include <stddef.h>
#include <stdint.h>

#include "canopus_manager_target.h"
#ifndef CANOPUS_MGR_BAND11
#include "canopus_target_config.h"
#else /* CANOPUS_MGR_BAND11 */
#include "band11/canopus_band11_abi.h"
#include "canopus_veneer.h"
#endif /* CANOPUS_MGR_BAND11 */
#include "canopus_client.h"
#include "canopus_manager_native.h"
#include "canopus_memory.h"
#include "canopus_supervisor.h"

#define CANOPUS_TARGET_PAGE_COUNT 3u
#define CANOPUS_TARGET_PAGE_OVERVIEW 0u
#define CANOPUS_TARGET_PAGE_MODULES 1u
#define CANOPUS_TARGET_PAGE_DETAIL 2u
#define CANOPUS_TARGET_UI_MAX_ROWS 25u
#define CANOPUS_TARGET_UI_MAX_LABELS 6u
#define CANOPUS_TARGET_UI_MAX_IMAGES 2u
#define CANOPUS_TARGET_UI_MAX_PROGRESS 4u
#define CANOPUS_TARGET_ROW_STATUS 1u
#define CANOPUS_TARGET_ROW_ACTION 2u
#define CANOPUS_TARGET_ROW_SWITCH 3u
#define CANOPUS_TARGET_TRAILING_NONE 0u
#define CANOPUS_TARGET_TRAILING_SWITCH 1u
#define CANOPUS_TARGET_TRAILING_FORWARD 3u
#define CANOPUS_TARGET_ALIGN_TOP_MID 2u
#define CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID 14u
#ifndef CANOPUS_MGR_BAND11
#define CANOPUS_TARGET_CONTENT_TOP_OFFSET 56
#define CANOPUS_TARGET_CONTENT_WIDTH 336
#define CANOPUS_TARGET_CONTENT_HEIGHT 424
#else /* CANOPUS_MGR_BAND11 */
#define CANOPUS_TARGET_CONTENT_TOP_OFFSET 68
#define CANOPUS_TARGET_CONTENT_WIDTH 212
#define CANOPUS_TARGET_CONTENT_HEIGHT 444
#define CANOPUS_TARGET_ROW_WIDTH 204
#endif /* CANOPUS_MGR_BAND11 */
#define CANOPUS_TARGET_ROW_GAP 8
#define CANOPUS_TARGET_EVENT_ALL 0u
#define CANOPUS_TARGET_EVENT_CLICKED 7u
#define CANOPUS_TARGET_EVENT_VALUE_CHANGED 30u

struct canopus_target_page;

typedef int (*page_signal_fn)(struct canopus_target_page *, uint32_t,
                              void *);
typedef int (*page_create_fn)(struct canopus_target_page *, void *,
                              void *);
typedef int (*page_lifecycle_fn)(struct canopus_target_page *);
typedef int (*page_foreground_data_fn)(struct canopus_target_page *,
                                       void *);

struct canopus_target_page {
    void *parent_descriptor;                 /* +0x00 */
    uint8_t _pad_04[12];                     /* +0x04 */
    const char *page_name;                    /* +0x10 */
    uint16_t page_id;                         /* +0x14 */
    uint16_t app_id;                          /* +0x16 */
    uint16_t flags;                           /* +0x18 */
    uint8_t _pad_1a[2];                      /* +0x1a */
    int32_t scheduler_deadline;               /* +0x1c */
    int32_t scheduler_priority;               /* +0x20 */
    uint32_t async_destroy_state;             /* +0x24 */
    uint8_t lifecycle_state;                  /* +0x28 */
    uint8_t layer;                            /* +0x29 */
    uint8_t page_kind;                        /* +0x2a */
    uint8_t _pad_2b;                         /* +0x2b */
    void *activity_context;                   /* +0x2c */
    void *root_object;                        /* +0x30 */
    page_signal_fn on_signal;                 /* +0x34 */
    void *runtime_default_56;                 /* +0x38 */
    uint8_t _pad_3c[4];                      /* +0x3c */
    void *registry_prev;                      /* +0x40 */
    void *registry_next;                      /* +0x44 */
    void *runtime_parent;                     /* +0x48 */
    page_create_fn on_create;                 /* +0x4c */
    page_lifecycle_fn on_resume;              /* +0x50 */
    page_foreground_data_fn on_foreground_data; /* +0x54 */
#ifndef CANOPUS_MGR_BAND11
    /* Slot order below follows the recovered type record
     * (targets/<id>/types/canopus_target_page.json) and the exact
     * 3.101.043 IDB: on_destroy_wrapped dispatches descriptor word 23
     * (+0x5c) and on_resume_wrapped dispatches word 22 (+0x58). */
    page_lifecycle_fn on_pause;               /* +0x58 */
    page_lifecycle_fn on_destroy;             /* +0x5c */
    page_lifecycle_fn on_ui_destroy;          /* +0x60 */
    void *extension_callback_100;             /* +0x64 */
    void *extension_callback_104;             /* +0x68 */
    void *extension_callback_108;             /* +0x6c */
    void *get_scroll_obj;                     /* +0x70, not in the type record */
#else /* CANOPUS_MGR_BAND11 */
    void *on_intermediate;                  /* +0x58 */
    page_lifecycle_fn on_pause;              /* +0x5c, 0xc693a0e */
    page_lifecycle_fn on_stop;               /* +0x60, 0xc693a5c */
    page_lifecycle_fn on_ui_destroy;            /* +0x64, 0xc696aa4 (UI destruction) */
    void *extension_callback_104;            /* +0x68 */
    void *extension_callback_108;            /* +0x6c */
    void *extension_callback_112;            /* +0x70 */
    void *extension_callback_116;            /* +0x74 */
#endif /* CANOPUS_MGR_BAND11 */
};

struct firmware_app_descriptor {
#ifndef CANOPUS_MGR_BAND11
    uint32_t registry_prev;                   /* +0x00 */
    uint32_t registry_next;                   /* +0x04 */
    const char *package_name;                  /* +0x08 */
    const char *launcher_icon_resource;        /* +0x0c */
    uint16_t app_id;                          /* +0x10 */
    uint8_t flags;                            /* +0x12 */
    uint8_t _pad_13;                         /* +0x13 */
    const char *owned_string_20;               /* +0x14 */
    const char *owned_string_24;               /* +0x18 */
    const char *(*launcher_metadata_callback)(void); /* +0x1c */
    uint8_t _pad_20[16];                     /* +0x20 */
    void *page_registry;                      /* +0x30 */
    uint8_t _pad_34[8];                      /* +0x34 */
    uint8_t hidden_flags;                     /* +0x3c */
    uint8_t _tail_3d[3];                     /* +0x3d */
#else /* CANOPUS_MGR_BAND11 */
    void *prev;                              /* +0x00 */
    void *next;                              /* +0x04 */
    void *vtable;                            /* +0x08, defaulted by app_install */
    const char *package_name;                /* +0x0c */
    const char *launcher_icon_resource;      /* +0x10 */
    uint16_t app_id;                         /* +0x14 */
    uint8_t flags[6];                        /* +0x16, defaults +0x17/+0x18 */
    const char *string_1c;                   /* +0x1c */
    const char *string_20;                   /* +0x20 */
    const char *(*launcher_metadata_callback)(void); /* +0x24 */
    uint8_t pad_28[16];                      /* +0x28 */
    void *page_registry;                     /* +0x38 */
    uint8_t pad_3c[12];                      /* +0x3c */
    uint8_t hidden_flags;                    /* +0x48 */
    uint8_t tail[7];
#endif /* CANOPUS_MGR_BAND11 */
};

#ifndef CANOPUS_MGR_BAND11
struct firmware_notification_message {
    uint64_t message_id;                      /* +0x00 */
    uint32_t repeat_count;                    /* +0x08 */
    const char *title;                        /* +0x0c */
    const char *source;                       /* +0x10 */
    const char *body;                         /* +0x14 */
    const char *auxiliary_text;               /* +0x18 */
    const char *small_icon_path;              /* +0x1c */
    const char *large_icon_path;              /* +0x20 */
    const char *extension_text_36;             /* +0x24 */
    const char *extension_text_40;             /* +0x28 */
    uint32_t timestamp;                       /* +0x2c */
    uint8_t _pad_30[8];                      /* +0x30 */
    void *action_callback;                    /* +0x38 */
    uint32_t action_context;                  /* +0x3c */
    uint32_t extension_64;                    /* +0x40 */
    uint32_t extension_68;                    /* +0x44 */
    void *open_callback;                      /* +0x48 */
    void *destroy_callback;                   /* +0x4c */
    uint8_t start_reminder;                   /* +0x50 */
    uint8_t flags_81;                         /* +0x51 */
    uint8_t flags_82;                         /* +0x52 */
    uint8_t _pad_53;                         /* +0x53 */
    void *callback_data;                      /* +0x54 */
};

#endif /* CANOPUS_MGR_BAND11 */
struct canopus_target_ui_binding {
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
};

struct canopus_target_msgbox_button {
    void (*callback)(void *event);
    const char *text;
    void *user_data;
    const void *visual;
    uint32_t reserved;
};

struct canopus_target_dialog {
    void *native_box;
    struct canopus_target_msgbox_button buttons[2];
    struct canopus_target_ui_binding cancel_binding;
    struct canopus_target_ui_binding confirm_binding;
    uint32_t generation;
    canopus_ui_node_id confirm_key;
    uint32_t confirm_event;
    canopus_ui_node_id cancel_key;
    uint32_t cancel_event;
    uint8_t active;
    uint8_t dispatching;
};

struct canopus_target_ui_backend {
    void *root;
    void *page_shell;
    void *page_title;
    void *content_root;
    void *rows[CANOPUS_TARGET_UI_MAX_ROWS];
#ifdef CANOPUS_MGR_BAND11
    void *row_event_objects[CANOPUS_TARGET_UI_MAX_ROWS];
#endif /* CANOPUS_MGR_BAND11 */
    void *labels[CANOPUS_TARGET_UI_MAX_LABELS];
    void *images[CANOPUS_TARGET_UI_MAX_IMAGES];
    void *progress[CANOPUS_TARGET_UI_MAX_PROGRESS];
    uint32_t image_resources[CANOPUS_TARGET_UI_MAX_IMAGES];
    int32_t progress_minimum[CANOPUS_TARGET_UI_MAX_PROGRESS];
    int32_t progress_maximum[CANOPUS_TARGET_UI_MAX_PROGRESS];
    int32_t progress_value[CANOPUS_TARGET_UI_MAX_PROGRESS];
    uint8_t row_kinds[CANOPUS_TARGET_UI_MAX_ROWS];
#ifdef CANOPUS_MGR_BAND11
    canopus_ui_node_id row_keys[CANOPUS_TARGET_UI_MAX_ROWS];
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_ui_binding bindings[CANOPUS_TARGET_UI_MAX_ROWS];
    struct canopus_target_page *firmware_page;
    struct canopus_target_dialog dialog;
    uint32_t row_count;
    uint32_t label_count;
    uint32_t rendered_generation;
    uint8_t page_index;
};

struct canopus_target_page_context {
    struct canopus_target_ui_backend backend;
    struct canopus_manager_native_v1 native;
    uint8_t active;
    uint8_t interactive;
};

static struct canopus_manager_model_v1 manager_model;
static struct canopus_client_v1 manager_client;
static struct canopus_target_page_context
    manager_pages[CANOPUS_TARGET_PAGE_COUNT];
static struct canopus_target_page
    manager_pages_desc[CANOPUS_TARGET_PAGE_COUNT];
static uint32_t manager_active_pages;
static uint32_t manager_pending_detail;
static uint8_t manager_session_ready;

#ifndef CANOPUS_MGR_BAND11
static void target_page_title_back(void *event)
{
    typedef uintptr_t (*get_user_data_fn)(void *);
    typedef int32_t (*page_finish_fn)(void *);
    get_user_data_fn get_user_data =
        (get_user_data_fn)(uintptr_t)FW_LVX_EVENT_GET_USER_DATA;
    struct canopus_target_page_context *context =
        (struct canopus_target_page_context *)get_user_data(event);
    page_finish_fn page_finish =
        (page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH;

    if (context == NULL || !context->active || !context->interactive ||
        context->backend.page_index == CANOPUS_TARGET_PAGE_OVERVIEW) {
        return;
    }
    context->interactive = 0u;
    (void)page_finish(&manager_pages_desc[context->backend.page_index]);
}

#endif /* CANOPUS_MGR_BAND11 */
_Static_assert(CANOPUS_MANAGER_TARGET_APP_ID <= UINT16_C(0x00FF),
               "system launch animation requires an 8-bit app id");
#ifndef CANOPUS_MGR_BAND11
_Static_assert(sizeof(struct canopus_target_page) == 116,
#else /* CANOPUS_MGR_BAND11 */
_Static_assert(sizeof(struct canopus_target_page) == 120,
#endif /* CANOPUS_MGR_BAND11 */
               "firmware page descriptor size");
_Static_assert(offsetof(struct canopus_target_page, page_name) == 16,
               "page name offset");
_Static_assert(offsetof(struct canopus_target_page, root_object) == 48,
               "page root offset");
_Static_assert(offsetof(struct canopus_target_page, on_create) == 76,
               "page create offset");
#ifndef CANOPUS_MGR_BAND11
_Static_assert(offsetof(struct canopus_target_page, on_destroy) == 92,
#else /* CANOPUS_MGR_BAND11 */
_Static_assert(offsetof(struct canopus_target_page, on_ui_destroy) == 100,
#endif /* CANOPUS_MGR_BAND11 */
               "page destroy offset");
#ifndef CANOPUS_MGR_BAND11
_Static_assert(sizeof(struct firmware_app_descriptor) == 64,
#else /* CANOPUS_MGR_BAND11 */
_Static_assert(sizeof(struct firmware_app_descriptor) == 80,
#endif /* CANOPUS_MGR_BAND11 */
               "firmware app descriptor size");
#ifndef CANOPUS_MGR_BAND11
_Static_assert(sizeof(struct firmware_notification_message) == 88,
               "firmware notification message size");
#else /* CANOPUS_MGR_BAND11 */
_Static_assert(sizeof(struct firmware_notification_message) == 96,
               "firmware notification size");
#endif /* CANOPUS_MGR_BAND11 */
_Static_assert(offsetof(struct firmware_notification_message, title) == 12,
               "notification title offset");
#ifndef CANOPUS_MGR_BAND11
_Static_assert(offsetof(struct firmware_notification_message, small_icon_path) == 28,
               "notification icon offset");
_Static_assert(offsetof(struct firmware_notification_message, start_reminder) == 80,
#else /* CANOPUS_MGR_BAND11 */
_Static_assert(offsetof(struct firmware_notification_message, timestamp) == 48,
               "notification timestamp offset");
_Static_assert(offsetof(struct firmware_notification_message, start_reminder) == 88,
#endif /* CANOPUS_MGR_BAND11 */
               "notification reminder offset");
#ifdef CANOPUS_MGR_BAND11
_Static_assert(offsetof(struct firmware_notification_message, callback_data) == 92,
               "notification callback data offset");
#endif /* CANOPUS_MGR_BAND11 */

static const char package_name[] = "com.canopus.manager";
static const char page_name_overview[] = "main";
static const char page_name_modules[] = "modules";
static const char page_name_detail[] = "module_detail";
static const char display_name[] = "Canopus 管理器";
#ifndef CANOPUS_MGR_BAND11
static const char empty_detail[] = "";
static const char notification_title[] = "Canopus";
static const char notification_body[] = "Canopus 已加载！尽情享受吧～";
static const char module_notification_body[] =
    "新模块已安装但处于禁用状态。打开 Canopus 管理器即可启用。";
/* The watchface bootstrap stages the LVGL v9 ARGB8888 bin (alpha preserved)
 * at this stable path; used for both the notification icons and the app icon. */
static const char notification_icon[] = "/data/canopus/manager_icon.bin";
#endif /* CANOPUS_MGR_BAND11 */
static const char launcher_icon[] = "/data/canopus/manager_icon.bin";

__attribute__((used, visibility("default"), section(".data.canopus_manager_target")))
volatile struct canopus_manager_target_record canopus_manager_target_record;

void canopus_manager_target_render_diagnostics(uint8_t out[36])
{
    const volatile uint32_t *source =
        &canopus_manager_target_record.build_id;
    uint32_t i;

#ifndef CANOPUS_MGR_BAND11
    if (out == NULL) {
        return;
    }
#else /* CANOPUS_MGR_BAND11 */
    if (out == NULL) return;
#endif /* CANOPUS_MGR_BAND11 */
    for (i = 0u; i < 9u; i++) {
        uint32_t value = source[i];
        out[i * 4u] = (uint8_t)value;
        out[i * 4u + 1u] = (uint8_t)(value >> 8);
        out[i * 4u + 2u] = (uint8_t)(value >> 16);
        out[i * 4u + 3u] = (uint8_t)(value >> 24);
    }
}

static int strings_differ(const char *left, const char *right)
{
    while (*left != '\0' && *right != '\0') {
        if (*left != *right) {
            return 1;
        }
        ++left;
        ++right;
    }
    return *left != *right;
}

static int identity_guard(void)
{
#ifndef CANOPUS_MGR_BAND11
    const char *version = (const char *)(uintptr_t)FW_VERSION_ADDRESS;
    const char *build = (const char *)(uintptr_t)FW_BUILD_ADDRESS;

    if (strings_differ(version, CANOPUS_TARGET_FIRMWARE_VERSION) != 0) {
        return -1;
    }
    if (strings_differ(build, CANOPUS_TARGET_FIRMWARE_BUILD) != 0) {
        return -2;
    }
    return 0;
#else /* CANOPUS_MGR_BAND11 */
    return canopus_identity_guard();
#endif /* CANOPUS_MGR_BAND11 */
}

static const char *manager_display_name(void)
{
    return display_name;
}

static int manager_on_signal(struct canopus_target_page *page,
                             uint32_t event, void *payload)
{
    (void)page;
    (void)event;
    (void)payload;
    return 0;
}

static int32_t target_device_open(void *cookie, const char *path)
{
    typedef int (*open_fn)(const char *, int, ...);
    open_fn open_device = (open_fn)(uintptr_t)FW_NUTTX_OPEN;
    (void)cookie;
    return open_device(path, 3); /* O_RDWR in this NuttX image. */
}

static int32_t target_device_close(void *cookie, int32_t fd)
{
    typedef int (*close_fn)(int);
    close_fn close_device = (close_fn)(uintptr_t)FW_NUTTX_CLOSE;
    (void)cookie;
    return close_device(fd);
}

static int32_t target_device_read(void *cookie, int32_t fd, void *buffer,
                                  uint32_t count)
{
    typedef int32_t (*read_fn)(int, void *, uint32_t);
    read_fn read_device = (read_fn)(uintptr_t)FW_NUTTX_READ;
    (void)cookie;
    return read_device(fd, buffer, count);
}

static int32_t target_device_write(void *cookie, int32_t fd,
                                   const void *buffer, uint32_t count)
{
    typedef int32_t (*write_fn)(int, const void *, uint32_t);
    write_fn write_device = (write_fn)(uintptr_t)FW_NUTTX_WRITE;
    (void)cookie;
    return write_device(fd, buffer, count);
}

static const struct canopus_client_io_v1 target_device_io = {
    sizeof(struct canopus_client_io_v1),
    CANOPUS_CLIENT_ABI_MAJOR,
    CANOPUS_CLIENT_ABI_MINOR,
    target_device_open,
    target_device_close,
    target_device_read,
    target_device_write,
};

static uint32_t target_next_request_id(void)
{
    uint32_t id = manager_model.next_request_id;
    manager_model.next_request_id++;
    if (id == 0u) {
        id = manager_model.next_request_id++;
    }
    if (manager_model.next_request_id == 0u) {
        manager_model.next_request_id = 1u;
    }
    return id;
}

static int target_refresh_model(void)
{
    struct canopus_client_device_snapshot_v1 device;
    char selected_id[CANOPUS_MANAGER_MODULE_ID_MAX];
    uint32_t old_view = manager_model.view;
    uint32_t slot;
    uint32_t found = 0u;
    uint32_t selected_found = 0u;

    canopus_memset(selected_id, 0, sizeof(selected_id));
    if (old_view == CANOPUS_MANAGER_VIEW_MODULE_DETAIL &&
        manager_model.selected < manager_model.module_count) {
        canopus_memcpy(selected_id,
                       manager_model.modules[manager_model.selected].module_id,
                       sizeof(selected_id));
    }
    if (canopus_client_query_device(&manager_client,
                                    target_next_request_id(), &device) !=
        CANOPUS_CLIENT_OK || device.module_count > CANOPUS_MANAGER_MAX_MODULES) {
        return -1;
    }
    manager_model.framework_revision = device.framework_revision;
    manager_model.safe_mode = device.safe_mode;
    manager_model.error_code = device.error_code;
    manager_model.supervisor_flags = device.flags;
    manager_model.reported_module_count = device.module_count;
    manager_model.module_query_error = 0;
    manager_model.module_query_error_slot = 0u;
    manager_model.module_count = 0u;
    for (slot = 0; slot < CANOPUS_MANAGER_MAX_MODULES; slot++) {
        struct canopus_client_module_snapshot_v1 source;
        struct canopus_manager_module_v1 module;
        {
            int32_t query_rc = canopus_client_query_module(
                &manager_client, target_next_request_id(), slot, &source);
            if (query_rc == CANOPUS_CLIENT_ERR_NOT_FOUND) {
                continue;
            }
            if (query_rc != CANOPUS_CLIENT_OK) {
                if (manager_model.module_query_error == 0 &&
                    found < device.module_count) {
                    manager_model.module_query_error = query_rc;
                    manager_model.module_query_error_slot = slot;
                }
                continue;
            }
        }
        canopus_memset(&module, 0, sizeof(module));
        canopus_memcpy(module.module_id, source.module_id,
                       sizeof(module.module_id));
        module.lifecycle_class = source.lifecycle_class;
        module.state = source.state;
        module.flags = source.flags;
        module.has_previous = (source.flags & CANOPUS_SUP_FLAG_HAS_PREV) != 0u;
        module.version = source.version;
        module.activation_error = source.activation_error;
        module.risk = source.lifecycle_class == CANOPUS_LIFECYCLE_REMOVABLE ?
            CANOPUS_MANAGER_RISK_MODERATE :
            CANOPUS_MANAGER_RISK_RESIDENT_CRITICAL;
        {
            int index = canopus_manager_upsert_module(&manager_model, &module);
            if (index < 0) {
                return -1;
            }
            if (selected_id[0] != '\0' &&
                strings_differ(selected_id, module.module_id) == 0) {
                manager_model.selected = (uint32_t)index;
                selected_found = 1u;
            }
        }
        found++;
    }
    if (old_view == CANOPUS_MANAGER_VIEW_MODULE_DETAIL && !selected_found) {
        manager_model.view = CANOPUS_MANAGER_VIEW_MODULE_LIST;
        manager_model.selected = 0u;
    }
    return 0;
}

static void target_dispatch_row(uint32_t row_index, void *event)
{
#ifndef CANOPUS_MGR_BAND11
    typedef uint32_t (*get_event_code_fn)(void *);
    get_event_code_fn get_code =
        (get_event_code_fn)(uintptr_t)FW_LVX_EVENT_GET_CODE;
#endif /* CANOPUS_MGR_BAND11 */
    uint32_t page_index;
    uint32_t code;

#ifndef CANOPUS_MGR_BAND11
    if (event == NULL || row_index >= CANOPUS_TARGET_UI_MAX_ROWS) {
        return;
    }
    code = get_code(event);
#else /* CANOPUS_MGR_BAND11 */
    if (event == NULL || row_index >= CANOPUS_TARGET_UI_MAX_ROWS) return;
    code = *(const uint16_t *)((const uint8_t *)event + 8u) & UINT16_C(0x7fff);
#endif /* CANOPUS_MGR_BAND11 */
    for (page_index = 0; page_index < CANOPUS_TARGET_PAGE_COUNT; page_index++) {
        struct canopus_target_page_context *context = &manager_pages[page_index];
        struct canopus_target_ui_binding *binding;
        int32_t rc;

        if (!context->active || !context->interactive ||
#ifndef CANOPUS_MGR_BAND11
            context->backend.rows[row_index] == NULL) {
            continue;
        }
        if (context->backend.row_kinds[row_index] == CANOPUS_TARGET_ROW_SWITCH) {
            if (code != CANOPUS_TARGET_EVENT_VALUE_CHANGED) {
                return;
            }
        } else if (code != CANOPUS_TARGET_EVENT_CLICKED) {
#else /* CANOPUS_MGR_BAND11 */
            context->backend.rows[row_index] == NULL ||
            *(void *const *)((const uint8_t *)event + 4u) !=
                context->backend.row_event_objects[row_index]) continue;
        if (code != (context->backend.row_kinds[row_index] ==
                     CANOPUS_TARGET_ROW_SWITCH ?
                     CANOPUS_TARGET_EVENT_VALUE_CHANGED :
                     CANOPUS_TARGET_EVENT_CLICKED)) {
#endif /* CANOPUS_MGR_BAND11 */
            return;
        }
        binding = &context->backend.bindings[row_index];
#ifndef CANOPUS_MGR_BAND11
        if (binding->event_id == 0u) {
            return;
        }
#else /* CANOPUS_MGR_BAND11 */
        if (binding->event_id == 0u) return;
#endif /* CANOPUS_MGR_BAND11 */
        canopus_manager_target_record.click_count += 1u;
        canopus_manager_target_record.clicked_row = row_index;
        canopus_manager_target_record.clicked_generation = binding->generation;
        canopus_manager_target_record.clicked_key = binding->key;
        canopus_manager_target_record.clicked_event = binding->event_id;
        canopus_manager_target_record.selected_before = manager_model.selected;
        rc = canopus_ui_dispatch_event(&context->native.ui,
                                       binding->generation,
                                       binding->key,
                                       binding->event_id);
        canopus_manager_target_record.selected_after = manager_model.selected;
        if (rc == CANOPUS_UI_ERR_DISABLED) {
            (void)canopus_manager_native_render(&context->native);
        }
        return;
    }
}

#define CANOPUS_TARGET_ROW_CALLBACK(index) \
    static void target_row_event_##index(void *event) \
    { \
        target_dispatch_row(index, event); \
    }

CANOPUS_TARGET_ROW_CALLBACK(0)
CANOPUS_TARGET_ROW_CALLBACK(1)
CANOPUS_TARGET_ROW_CALLBACK(2)
CANOPUS_TARGET_ROW_CALLBACK(3)
CANOPUS_TARGET_ROW_CALLBACK(4)
CANOPUS_TARGET_ROW_CALLBACK(5)
CANOPUS_TARGET_ROW_CALLBACK(6)
CANOPUS_TARGET_ROW_CALLBACK(7)
CANOPUS_TARGET_ROW_CALLBACK(8)
CANOPUS_TARGET_ROW_CALLBACK(9)
CANOPUS_TARGET_ROW_CALLBACK(10)
CANOPUS_TARGET_ROW_CALLBACK(11)
CANOPUS_TARGET_ROW_CALLBACK(12)
CANOPUS_TARGET_ROW_CALLBACK(13)
CANOPUS_TARGET_ROW_CALLBACK(14)
CANOPUS_TARGET_ROW_CALLBACK(15)
#ifdef CANOPUS_MGR_BAND11
CANOPUS_TARGET_ROW_CALLBACK(16)
CANOPUS_TARGET_ROW_CALLBACK(17)
CANOPUS_TARGET_ROW_CALLBACK(18)
CANOPUS_TARGET_ROW_CALLBACK(19)
CANOPUS_TARGET_ROW_CALLBACK(20)
CANOPUS_TARGET_ROW_CALLBACK(21)
CANOPUS_TARGET_ROW_CALLBACK(22)
CANOPUS_TARGET_ROW_CALLBACK(23)
CANOPUS_TARGET_ROW_CALLBACK(24)
#endif /* CANOPUS_MGR_BAND11 */

static void (*const target_row_events[CANOPUS_TARGET_UI_MAX_ROWS])(void *) = {
    target_row_event_0, target_row_event_1, target_row_event_2,
    target_row_event_3, target_row_event_4, target_row_event_5,
    target_row_event_6, target_row_event_7, target_row_event_8,
    target_row_event_9, target_row_event_10, target_row_event_11,
    target_row_event_12, target_row_event_13, target_row_event_14,
    target_row_event_15,
#ifdef CANOPUS_MGR_BAND11
    target_row_event_16,
    target_row_event_17,
    target_row_event_18,
    target_row_event_19,
    target_row_event_20,
    target_row_event_21,
    target_row_event_22,
    target_row_event_23,
    target_row_event_24,
#endif /* CANOPUS_MGR_BAND11 */
};

#undef CANOPUS_TARGET_ROW_CALLBACK

static uint8_t target_row_kind(uint16_t node_type)
{
    if (node_type == CANOPUS_UI_NODE_SWITCH_ROW ||
        node_type == CANOPUS_UI_NODE_CHECKBOX) {
        return CANOPUS_TARGET_ROW_SWITCH;
    }
    if (node_type == CANOPUS_UI_NODE_BUTTON ||
        node_type == CANOPUS_UI_NODE_ACTION_ROW ||
        node_type == CANOPUS_UI_NODE_RADIO_ROW) {
        return CANOPUS_TARGET_ROW_ACTION;
    }
    return CANOPUS_TARGET_ROW_STATUS;
}

#ifdef CANOPUS_MGR_BAND11
static int target_snapshot_uses_row(
    const struct canopus_ui_snapshot_v1 *snapshot, uint8_t kind,
    canopus_ui_node_id key)
{
    uint16_t i;
    for (i = 0u; i < snapshot->node_count; i++) {
        if (snapshot->nodes[i].key == key &&
            target_row_kind(snapshot->nodes[i].type) == kind &&
            snapshot->nodes[i].type != CANOPUS_UI_NODE_SECTION &&
            snapshot->nodes[i].type != CANOPUS_UI_NODE_NAVIGATION_PAGE &&
            snapshot->nodes[i].type != CANOPUS_UI_NODE_TEXT) {
            return 1;
        }
    }
    return 0;
}

#endif /* CANOPUS_MGR_BAND11 */
static int target_find_row(struct canopus_target_ui_backend *backend,
#ifndef CANOPUS_MGR_BAND11
                           uint8_t kind, uint32_t used_mask)
#else /* CANOPUS_MGR_BAND11 */
                           const struct canopus_ui_snapshot_v1 *snapshot,
                           uint8_t kind, canopus_ui_node_id key,
                           uint32_t used_mask)
#endif /* CANOPUS_MGR_BAND11 */
{
    uint32_t i;
#ifdef CANOPUS_MGR_BAND11
    int reusable = -1;
#endif /* CANOPUS_MGR_BAND11 */
    int empty = -1;

    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] == NULL) {
#ifndef CANOPUS_MGR_BAND11
            if (empty < 0) {
                empty = (int)i;
            }
#else /* CANOPUS_MGR_BAND11 */
            if (empty < 0) empty = (int)i;
#endif /* CANOPUS_MGR_BAND11 */
        } else if (backend->row_kinds[i] == kind &&
                   (used_mask & (UINT32_C(1) << i)) == 0u) {
#ifndef CANOPUS_MGR_BAND11
            return (int)i;
#else /* CANOPUS_MGR_BAND11 */
            if (backend->row_keys[i] == key) return (int)i;
            if (reusable < 0 && !target_snapshot_uses_row(
                    snapshot, kind, backend->row_keys[i])) {
                reusable = (int)i;
            }
#endif /* CANOPUS_MGR_BAND11 */
        }
    }
#ifndef CANOPUS_MGR_BAND11
    return empty;
#else /* CANOPUS_MGR_BAND11 */
    return reusable >= 0 ? reusable : empty;
#endif /* CANOPUS_MGR_BAND11 */
}

#ifndef CANOPUS_MGR_BAND11
static int target_find_confirmation(
    const struct canopus_ui_snapshot_v1 *snapshot,
    const struct canopus_ui_node_v1 **message,
    const struct canopus_ui_node_v1 **confirm,
    const struct canopus_ui_node_v1 **cancel)
#else /* CANOPUS_MGR_BAND11 */
static void target_set_hidden(void *object, uint32_t hidden)
#endif /* CANOPUS_MGR_BAND11 */
{
#ifndef CANOPUS_MGR_BAND11
    uint16_t i;
    *message = NULL;
    *confirm = NULL;
    *cancel = NULL;
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        if (node->type == CANOPUS_UI_NODE_TEXT) {
            *message = node;
        } else if (node->event_id == CANOPUS_MANAGER_EVENT_CONFIRM) {
            *confirm = node;
        } else if (node->event_id == CANOPUS_MANAGER_EVENT_CANCEL) {
            *cancel = node;
        }
    }
    return *message != NULL && *confirm != NULL && *cancel != NULL;
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*flag_fn)(void *, uint32_t);
    flag_fn set_flag = (flag_fn)(uintptr_t)(hidden != 0u
        ? FW_LV_OBJECT_ADD_FLAG : FW_LV_OBJECT_CLEAR_FLAG);
    if (object != NULL) set_flag(object, UINT32_C(1));
#endif /* CANOPUS_MGR_BAND11 */
}

#ifndef CANOPUS_MGR_BAND11
static void target_dialog_dismiss(struct canopus_target_ui_backend *backend)
{
    typedef void (*delete_fn)(void *);
    delete_fn delete_object = (delete_fn)(uintptr_t)FW_LVX_OBJECT_DELETE;
    void *box;
    if (backend == NULL || !backend->dialog.active) return;
    box = backend->dialog.native_box;
    backend->dialog.active = 0u;
    backend->dialog.native_box = NULL;
    if (box != NULL) delete_object(box);
}

static void target_dialog_event(void *event)
{
    typedef uintptr_t (*get_user_data_fn)(void *);
    get_user_data_fn get_user_data =
        (get_user_data_fn)(uintptr_t)FW_LVX_EVENT_GET_USER_DATA;
    struct canopus_target_ui_binding *binding =
        (struct canopus_target_ui_binding *)get_user_data(event);
    struct canopus_target_page_context *context;
    uint32_t page_index;

    if (binding == NULL) return;
    /* The callback stores the page index in the binding key's high byte and
     * the semantic key in its low 24 bits. */
    page_index = binding->key >> 24;
    if (page_index >= CANOPUS_TARGET_PAGE_COUNT) return;
    context = &manager_pages[page_index];
    if (!context->active || !context->interactive ||
        context->backend.dialog.dispatching) return;
    context->backend.dialog.dispatching = 1u;
    target_dialog_dismiss(&context->backend);
    (void)canopus_ui_dispatch_event(&context->native.ui,
                                    binding->generation,
                                    binding->key & UINT32_C(0x00FFFFFF),
                                    binding->event_id);
    context->backend.dialog.dispatching = 0u;
}

static int target_show_confirmation(
    struct canopus_target_ui_backend *backend,
    const struct canopus_ui_snapshot_v1 *snapshot,
    const struct canopus_ui_node_v1 *message,
    const struct canopus_ui_node_v1 *confirm,
    const struct canopus_ui_node_v1 *cancel)
{
    typedef void *(*create_fn)(void *, void *);
    typedef void (*set_content_fn)(
        void *, uint32_t, const char *, const char *, const char *,
        const struct canopus_target_msgbox_button *, uint32_t);
    create_fn create_box = (create_fn)(uintptr_t)FW_LVX_MSGBOX_CREATE;
    set_content_fn set_content =
        (set_content_fn)(uintptr_t)FW_LVX_MSGBOX_SET_CONTENT;
    struct canopus_target_ui_binding *cancel_binding;
    struct canopus_target_ui_binding *confirm_binding;
    const char *text;

    if (backend->dialog.active) return 0;
    if (backend->root == NULL || backend->firmware_page == NULL) return -1;
    canopus_memset(&backend->dialog, 0, sizeof(backend->dialog));
    backend->dialog.generation = snapshot->generation;
    backend->dialog.confirm_key = confirm->key;
    backend->dialog.confirm_event = confirm->event_id;
    backend->dialog.cancel_key = cancel->key;
    backend->dialog.cancel_event = cancel->event_id;
    cancel_binding = &backend->dialog.cancel_binding;
    confirm_binding = &backend->dialog.confirm_binding;
    cancel_binding->generation = snapshot->generation;
    cancel_binding->key = ((uint32_t)backend->page_index << 24) | cancel->key;
    cancel_binding->event_id = cancel->event_id;
    confirm_binding->generation = snapshot->generation;
    confirm_binding->key = ((uint32_t)backend->page_index << 24) | confirm->key;
    confirm_binding->event_id = confirm->event_id;
    backend->dialog.buttons[0].callback = target_dialog_event;
    backend->dialog.buttons[0].text = "取消";
    backend->dialog.buttons[0].user_data = cancel_binding;
    backend->dialog.buttons[1].callback = target_dialog_event;
    backend->dialog.buttons[1].text = "确认";
    backend->dialog.buttons[1].user_data = confirm_binding;
    /* The stock prefab sizes its modal scrim from its parent. Parenting it to
     * content_root confines it to the scrollable page body, so the dialog
     * scrolls and the page title remains above it. The firmware page root is
     * the full-screen owner; a newly created root child is also frontmost over
     * the existing title and content children. */
    backend->dialog.native_box = create_box(backend->root,
                                             backend->firmware_page);
    if (backend->dialog.native_box == NULL) return -1;
    backend->dialog.active = 1u;
    text = snapshot->strings + message->primary_off;
    set_content(backend->dialog.native_box, 2u, NULL, text, NULL,
                backend->dialog.buttons, 2u);
    return 0;
}

#endif /* CANOPUS_MGR_BAND11 */
static int32_t target_ui_apply(
    void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
#ifndef CANOPUS_MGR_BAND11
    typedef void *(*create_row_fn)(void *, const char *, const char *, uint8_t);
    typedef int (*update_row_fn)(void *, const char *, const char *,
                                 const char *, int32_t, uint8_t);
    typedef void *(*get_trailing_fn)(void *);
#else /* CANOPUS_MGR_BAND11 */
    typedef void *(*object_create_fn)(void *);
#endif /* CANOPUS_MGR_BAND11 */
    typedef void *(*create_label_fn)(void *);
    typedef void (*set_label_text_fn)(void *, const char *);
#ifndef CANOPUS_MGR_BAND11
    typedef void *(*create_content_fn)(void *);
    typedef void (*set_size_fn)(void *, int32_t, int32_t);
#ifdef FW_LVX_CONTENT_PAD_BOTTOM
    typedef void (*set_content_pad_bottom_fn)(void *, int32_t, uint32_t);
#endif
    typedef void (*align_fn)(void *, uint32_t, int32_t, int32_t);
#endif /* CANOPUS_MGR_BAND11 */
    typedef void *(*create_page_title_fn)(void *, const char *, uint32_t,
#ifndef CANOPUS_MGR_BAND11
                                          void (*)(void *), void *);
    typedef int (*apply_style_fn)(void *, const void *, uint32_t, uint32_t);
#else /* CANOPUS_MGR_BAND11 */
                                          void (*)(void *));
#endif /* CANOPUS_MGR_BAND11 */
    typedef void (*add_event_fn)(void *, void (*)(void *), uint32_t, void *);
#ifndef CANOPUS_MGR_BAND11
    typedef void (*set_hidden_fn)(void *, uint32_t);
#endif /* CANOPUS_MGR_BAND11 */
    typedef void (*align_to_fn)(void *, void *, uint32_t, int32_t, int32_t);
#ifdef CANOPUS_MGR_BAND11
    typedef void (*set_size_fn)(void *, int32_t, int32_t);
#endif /* CANOPUS_MGR_BAND11 */
    typedef void *(*image_create_fn)(void *);
    typedef void (*image_set_src_fn)(void *, const void *);
    typedef void *(*bar_create_fn)(void *);
    typedef void (*bar_set_range_fn)(void *, int32_t, int32_t);
    typedef void (*bar_set_value_fn)(void *, int32_t, uint32_t);
#ifdef CANOPUS_MGR_BAND11
    typedef void *(*row_add_content_fn)(void *, const void *, const char *,
                                        const char *, int32_t, uint8_t, uint8_t);
    typedef void (*row_update_fn)(void *, const void *, const char *,
                                  const char *, int32_t, uint8_t);
    typedef void (*style_apply_fn)(void *, const void *, uint32_t, uint32_t);
    typedef void (*style_value_fn)(void *, int32_t, uint32_t);
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_ui_backend *backend =
        (struct canopus_target_ui_backend *)cookie;
#ifndef CANOPUS_MGR_BAND11
    create_row_fn create_row =
        (create_row_fn)(uintptr_t)FW_LVX_LIST_ROW_CREATE;
    update_row_fn update_row =
        (update_row_fn)(uintptr_t)FW_LVX_LIST_ROW_UPDATE;
    get_trailing_fn get_trailing =
        (get_trailing_fn)(uintptr_t)FW_LVX_LIST_ROW_TRAILING;
#else /* CANOPUS_MGR_BAND11 */
    object_create_fn object_create =
        (object_create_fn)(uintptr_t)FW_LV_OBJECT_CREATE;
#endif /* CANOPUS_MGR_BAND11 */
    create_label_fn create_label =
#ifndef CANOPUS_MGR_BAND11
        (create_label_fn)(uintptr_t)FW_LVX_LABEL_CREATE;
#else /* CANOPUS_MGR_BAND11 */
        (create_label_fn)(uintptr_t)FW_LV_LABEL_CREATE;
#endif /* CANOPUS_MGR_BAND11 */
    set_label_text_fn set_label_text =
#ifndef CANOPUS_MGR_BAND11
        (set_label_text_fn)(uintptr_t)FW_LVX_LABEL_SET_TEXT;
    create_content_fn create_content =
        (create_content_fn)(uintptr_t)FW_LVX_CONTENT_CREATE;
    set_size_fn set_size = (set_size_fn)(uintptr_t)FW_LVX_OBJECT_SET_SIZE;
#ifdef FW_LVX_CONTENT_PAD_BOTTOM
    set_content_pad_bottom_fn set_content_pad_bottom =
        (set_content_pad_bottom_fn)(uintptr_t)FW_LVX_CONTENT_PAD_BOTTOM;
#endif
    align_fn align = (align_fn)(uintptr_t)FW_LVX_OBJECT_ALIGN;
#else /* CANOPUS_MGR_BAND11 */
        (set_label_text_fn)(uintptr_t)FW_LV_LABEL_SET_TEXT;
#endif /* CANOPUS_MGR_BAND11 */
    create_page_title_fn create_page_title =
#ifndef CANOPUS_MGR_BAND11
        (create_page_title_fn)(uintptr_t)FW_LVX_PAGE_TITLE_CREATE;
    apply_style_fn apply_style =
        (apply_style_fn)(uintptr_t)FW_LVX_STYLE_APPLY;
#ifndef FW_STYLE_MISANS_DEMIBOLD_32
    (void)apply_style;
#endif
    add_event_fn add_event = (add_event_fn)(uintptr_t)FW_LVX_EVENT_ADD;
    set_hidden_fn set_hidden = (set_hidden_fn)(uintptr_t)FW_LVX_SET_HIDDEN;
    align_to_fn align_to = (align_to_fn)(uintptr_t)FW_LVX_ALIGN_TO;
#else /* CANOPUS_MGR_BAND11 */
        (create_page_title_fn)(uintptr_t)FW_LV_PAGE_TITLE_CREATE;
    add_event_fn add_event = (add_event_fn)(uintptr_t)FW_LV_EVENT_ADD;
    align_to_fn align_to = (align_to_fn)(uintptr_t)FW_LV_ALIGN_TO;
    set_size_fn set_size = (set_size_fn)(uintptr_t)FW_LV_OBJECT_SET_SIZE;
#endif /* CANOPUS_MGR_BAND11 */
    image_create_fn image_create =
#ifndef CANOPUS_MGR_BAND11
        (image_create_fn)(uintptr_t)FW_LVX_IMAGE_CREATE;
#else /* CANOPUS_MGR_BAND11 */
        (image_create_fn)(uintptr_t)FW_LV_IMAGE_CREATE;
#endif /* CANOPUS_MGR_BAND11 */
    image_set_src_fn image_set_src =
#ifndef CANOPUS_MGR_BAND11
        (image_set_src_fn)(uintptr_t)FW_LVX_IMAGE_SET_SRC;
    bar_create_fn bar_create = (bar_create_fn)(uintptr_t)FW_LVX_BAR_CREATE;
#else /* CANOPUS_MGR_BAND11 */
        (image_set_src_fn)(uintptr_t)FW_LV_IMAGE_SET_SRC;
    bar_create_fn bar_create = (bar_create_fn)(uintptr_t)FW_LV_BAR_CREATE;
#endif /* CANOPUS_MGR_BAND11 */
    bar_set_range_fn bar_set_range =
#ifndef CANOPUS_MGR_BAND11
        (bar_set_range_fn)(uintptr_t)FW_LVX_BAR_SET_RANGE;
#else /* CANOPUS_MGR_BAND11 */
        (bar_set_range_fn)(uintptr_t)FW_LV_BAR_SET_RANGE;
#endif /* CANOPUS_MGR_BAND11 */
    bar_set_value_fn bar_set_value =
#ifndef CANOPUS_MGR_BAND11
        (bar_set_value_fn)(uintptr_t)FW_LVX_BAR_SET_VALUE;
#else /* CANOPUS_MGR_BAND11 */
        (bar_set_value_fn)(uintptr_t)FW_LV_BAR_SET_VALUE;
    object_create_fn create_row =
        (object_create_fn)(uintptr_t)FW_LVX_LIST_ITEM_CREATE;
    row_add_content_fn add_row_content =
        (row_add_content_fn)(uintptr_t)FW_LVX_LIST_ITEM_ADD_CONTENT;
    row_update_fn update_row =
        (row_update_fn)(uintptr_t)FW_LVX_LIST_ITEM_UPDATE;
    style_apply_fn apply_style =
        (style_apply_fn)(uintptr_t)FW_LVX_STYLE_APPLY;
#endif /* CANOPUS_MGR_BAND11 */
    uint16_t i;
    uint32_t visible_rows = 0u;
    uint32_t visible_labels = 0u;
    uint32_t visible_images = 0u;
    uint32_t visible_progress = 0u;
    uint32_t used_mask = 0u;
    uint32_t label_used = 0u;
    uint32_t image_used = 0u;
    uint32_t progress_used = 0u;
    void *previous = NULL;
    void *first = NULL;

    if (backend == NULL || backend->root == NULL || snapshot == NULL ||
        snapshot->node_count == 0u ||
        backend->page_index >= CANOPUS_TARGET_PAGE_COUNT) {
        return -1;
    }
#ifndef CANOPUS_MGR_BAND11
    {
        const struct canopus_ui_node_v1 *message;
        const struct canopus_ui_node_v1 *confirm;
        const struct canopus_ui_node_v1 *cancel;
        if (target_find_confirmation(snapshot, &message, &confirm, &cancel) &&
            target_show_confirmation(backend, snapshot, message,
                                     confirm, cancel) == 0) {
            return 0;
        }
    }
    target_dialog_dismiss(backend);
#endif /* CANOPUS_MGR_BAND11 */
    if (backend->content_root == NULL) {
#ifndef CANOPUS_MGR_BAND11
        backend->content_root = create_content(backend->root);
        if (backend->content_root == NULL) {
            return -1;
        }
        set_size(backend->content_root, CANOPUS_TARGET_CONTENT_WIDTH,
                 CANOPUS_TARGET_CONTENT_HEIGHT);
        align(backend->content_root, CANOPUS_TARGET_ALIGN_TOP_MID, 0,
              CANOPUS_TARGET_CONTENT_TOP_OFFSET);
#ifdef FW_LVX_CONTENT_PAD_BOTTOM
        set_content_pad_bottom(backend->content_root, 32, 0u);
#endif
#else /* CANOPUS_MGR_BAND11 */
        backend->content_root = object_create(backend->root);
        if (!backend->content_root) return -1;
        /* A transparent scrolling viewport. 0x0C693F78 is an activity root
         * with its own lifetime timer, not the 10 Pro content-container ABI. */
        ((void (*)(void *))(uintptr_t)FW_LV_OBJECT_REMOVE_STYLES)(backend->content_root);
        ((style_value_fn)(uintptr_t)FW_LV_OBJECT_PAD_TOP)(backend->content_root, 4, 0u);
        ((style_value_fn)(uintptr_t)FW_LV_OBJECT_PAD_BOTTOM)(backend->content_root, 32, 0u);
        set_size(backend->content_root, CANOPUS_TARGET_CONTENT_WIDTH, CANOPUS_TARGET_CONTENT_HEIGHT);
        align_to(backend->content_root, backend->root, CANOPUS_TARGET_ALIGN_TOP_MID, 0, CANOPUS_TARGET_CONTENT_TOP_OFFSET);
#endif /* CANOPUS_MGR_BAND11 */
    }
    for (i = 0; i < snapshot->node_count; i++) {
        uint16_t type = snapshot->nodes[i].type;
        if (type == CANOPUS_UI_NODE_SECTION ||
            type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
            type == CANOPUS_UI_NODE_NAVIGATION_HEADER ||
            type == CANOPUS_UI_NODE_DIVIDER ||
            type == CANOPUS_UI_NODE_SPACER) {
            continue;
        }
        if (type == CANOPUS_UI_NODE_TEXT) {
            visible_labels++;
            continue;
        }
        if (type == CANOPUS_UI_NODE_IMAGE) {
            visible_images++;
            continue;
        }
        if (type == CANOPUS_UI_NODE_PROGRESS ||
            type == CANOPUS_UI_NODE_SLIDER) {
            visible_progress++;
            continue;
        }
        if (type != CANOPUS_UI_NODE_STATUS_ROW &&
            type != CANOPUS_UI_NODE_BUTTON &&
            type != CANOPUS_UI_NODE_ACTION_ROW &&
            type != CANOPUS_UI_NODE_SWITCH_ROW &&
            type != CANOPUS_UI_NODE_CHECKBOX &&
            type != CANOPUS_UI_NODE_RADIO_ROW) {
            return -1;
        }
        visible_rows++;
    }
    if (visible_labels > CANOPUS_TARGET_UI_MAX_LABELS ||
        visible_rows > CANOPUS_TARGET_UI_MAX_ROWS ||
        visible_images > CANOPUS_TARGET_UI_MAX_IMAGES ||
        visible_progress > CANOPUS_TARGET_UI_MAX_PROGRESS) {
        return -1;
    }

    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        const char *primary;
#ifndef CANOPUS_MGR_BAND11
        const char *secondary = empty_detail;
#endif /* CANOPUS_MGR_BAND11 */
        uint8_t kind;
#ifndef CANOPUS_MGR_BAND11
        uint8_t trailing;
#endif /* CANOPUS_MGR_BAND11 */
        int slot;
        void *object;
        int32_t gap = CANOPUS_TARGET_ROW_GAP;

        if (node->type == CANOPUS_UI_NODE_SECTION ||
            node->type == CANOPUS_UI_NODE_DIVIDER ||
            node->type == CANOPUS_UI_NODE_SPACER) {
            continue;
        }
        primary = snapshot->strings + node->primary_off;
        if (node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE ||
            node->type == CANOPUS_UI_NODE_NAVIGATION_HEADER) {
            uint32_t title_mode = backend->page_index ==
                CANOPUS_TARGET_PAGE_OVERVIEW ? 0u : 1u;

            if (backend->page_title == NULL) {
                backend->page_title = create_page_title(
#ifndef CANOPUS_MGR_BAND11
                    backend->root, primary, title_mode,
                    title_mode != 0u ? target_page_title_back : NULL,
                    title_mode != 0u ? (void *)&manager_pages[
                        backend->page_index] : NULL);
#else /* CANOPUS_MGR_BAND11 */
                    backend->root, primary, title_mode, NULL);
#endif /* CANOPUS_MGR_BAND11 */
                if (backend->page_title == NULL) {
                    return -1;
                }
#ifdef CANOPUS_MGR_BAND11
                apply_style(backend->page_title,
                            (const void *)(uintptr_t)FW_STYLE_MISANS_MEDIUM_24,
                            255u, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            }
            /* lvx_page_title_create owns the stock left title, right-side time,
             * optional back affordance, and its standard content inset. It is
             * intentionally created once per firmware page: updating its title
             * contract is not yet recovered. */
#ifndef CANOPUS_MGR_BAND11
            set_hidden(backend->page_title, 0u);
            previous = backend->page_title;
#else /* CANOPUS_MGR_BAND11 */
            target_set_hidden(backend->page_title, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            first = backend->page_title;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_TEXT) {
            object = backend->labels[label_used];
            if (object == NULL) {
                object = create_label(backend->content_root);
                if (object == NULL) {
                    return -1;
                }
                backend->labels[label_used] = object;
                backend->label_count++;
            }
#ifdef CANOPUS_MGR_BAND11
            apply_style(object, (const void *)(uintptr_t)(
                            snapshot->styles[i].text_style == CANOPUS_UI_TEXT_TITLE ?
                            FW_STYLE_MISANS_MEDIUM_28 : FW_STYLE_MISANS_MEDIUM_24),
                        255u, 0u);
            ((void (*)(void *, int32_t))(uintptr_t)FW_LV_OBJECT_WIDTH)(object, CANOPUS_TARGET_ROW_WIDTH);
            ((void (*)(void *, uint32_t))(uintptr_t)FW_LV_LABEL_LONG_MODE)(object, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            set_label_text(object, primary);
#ifndef CANOPUS_MGR_BAND11
            if (snapshot->styles[i].text_style == CANOPUS_UI_TEXT_TITLE) {
#ifdef FW_STYLE_MISANS_DEMIBOLD_32
                /* lvx_theme_default_init maps this exact-target style object to
                 * MiSans-Demibold at 32 px. sub_C49EA98 is the stock helper used
                 * by firmware labels to replace the inherited text style. */
                (void)apply_style(
                    object,
                    (const void *)(uintptr_t)FW_STYLE_MISANS_DEMIBOLD_32,
                    255u, 0u);
#endif
            }
            set_hidden(object, 0u);
#else /* CANOPUS_MGR_BAND11 */
            target_set_hidden(object, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            if (previous == NULL) {
                align_to(object, backend->content_root,
                         CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
                first = object;
            } else {
                gap = node->type == CANOPUS_UI_NODE_TEXT ? 4 : 8;
                align_to(object, previous,
                         CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, gap);
            }
            previous = object;
            label_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_IMAGE) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            uint32_t resource = snapshot->values[i].resource_id;
            if (primary[0] == '\0' || resource == 0u ||
                layout->width <= 0 || layout->height <= 0) return -1;
            object = backend->images[image_used];
            if (object == NULL) {
                object = image_create(backend->content_root);
                if (object == NULL) return -1;
                backend->images[image_used] = object;
            }
            image_set_src(object, primary);
            backend->image_resources[image_used] = resource;
            set_size(object, layout->width, layout->height);
#ifndef CANOPUS_MGR_BAND11
            set_hidden(object, 0u);
#else /* CANOPUS_MGR_BAND11 */
            target_set_hidden(object, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            if (previous == NULL) {
                align_to(object, backend->content_root,
                         CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
                first = object;
            } else {
                align_to(object, previous,
                         CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, 8);
            }
            previous = object;
            image_used++;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_PROGRESS ||
            node->type == CANOPUS_UI_NODE_SLIDER) {
            const struct canopus_ui_layout_v1 *layout = &snapshot->layouts[i];
            const struct canopus_ui_value_v1 *value = &snapshot->values[i];
            if (layout->width <= 0 || layout->height <= 0 ||
                value->minimum >= value->maximum) return -1;
            object = backend->progress[progress_used];
            if (object == NULL) {
                object = bar_create(backend->content_root);
                if (object == NULL) return -1;
                backend->progress[progress_used] = object;
                bar_set_range(object, value->minimum, value->maximum);
                bar_set_value(object, value->value, 0u);
                backend->progress_minimum[progress_used] = value->minimum;
                backend->progress_maximum[progress_used] = value->maximum;
                backend->progress_value[progress_used] = value->value;
            } else {
                if (backend->progress_minimum[progress_used] != value->minimum ||
                    backend->progress_maximum[progress_used] != value->maximum) {
                    bar_set_range(object, value->minimum, value->maximum);
                    backend->progress_minimum[progress_used] = value->minimum;
                    backend->progress_maximum[progress_used] = value->maximum;
                }
                if (backend->progress_value[progress_used] != value->value) {
                    bar_set_value(object, value->value, 0u);
                    backend->progress_value[progress_used] = value->value;
                }
            }
            set_size(object, layout->width, layout->height);
#ifndef CANOPUS_MGR_BAND11
            set_hidden(object, 0u);
#else /* CANOPUS_MGR_BAND11 */
            target_set_hidden(object, 0u);
#endif /* CANOPUS_MGR_BAND11 */
            if (previous == NULL) {
                align_to(object, backend->content_root,
                         CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
                first = object;
            } else {
                align_to(object, previous,
                         CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, 8);
            }
            previous = object;
            progress_used++;
            continue;
        }
#ifndef CANOPUS_MGR_BAND11
        if (node->secondary_len != 0u) {
            secondary = snapshot->strings + node->secondary_off;
        }
#endif /* CANOPUS_MGR_BAND11 */
        kind = target_row_kind(node->type);
#ifndef CANOPUS_MGR_BAND11
        if (kind == CANOPUS_TARGET_ROW_ACTION) {
            trailing = CANOPUS_TARGET_TRAILING_FORWARD;
        } else if (kind == CANOPUS_TARGET_ROW_SWITCH) {
            trailing = CANOPUS_TARGET_TRAILING_SWITCH;
        } else {
            trailing = CANOPUS_TARGET_TRAILING_NONE;
        }
        slot = target_find_row(backend, kind, used_mask);
#else /* CANOPUS_MGR_BAND11 */
        slot = target_find_row(backend, snapshot, kind, node->key,
                               used_mask);
#endif /* CANOPUS_MGR_BAND11 */
        if (slot < 0) {
            return -1;
        }
        object = backend->rows[slot];
        if (object == NULL) {
#ifdef CANOPUS_MGR_BAND11
            uint8_t trailing = kind == CANOPUS_TARGET_ROW_SWITCH ?
                CANOPUS_TARGET_TRAILING_SWITCH : kind == CANOPUS_TARGET_ROW_ACTION ?
                CANOPUS_TARGET_TRAILING_FORWARD : CANOPUS_TARGET_TRAILING_NONE;
#endif /* CANOPUS_MGR_BAND11 */
            void *event_object;
#ifndef CANOPUS_MGR_BAND11
            uint32_t event_code;
            object = create_row(backend->content_root, primary, secondary, trailing);
#else /* CANOPUS_MGR_BAND11 */
            object = create_row(backend->content_root);
#endif /* CANOPUS_MGR_BAND11 */
            if (object == NULL) {
                return -1;
            }
#ifdef CANOPUS_MGR_BAND11
            /* The native class applies the settings theme: 204x96, radius 24,
             * #222 background, MiSans Medium 28/24 and grey secondary text.
             * Populate only once: repeating it would allocate duplicate children. */
            ((void (*)(void *, uint32_t))(uintptr_t)FW_LV_OBJECT_CLEAR_FLAG)(object, 16u);
            add_row_content(object, NULL, primary,
                            node->secondary_len ? snapshot->strings + node->secondary_off : NULL,
                            0, (node->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u,
                            trailing);
#endif /* CANOPUS_MGR_BAND11 */
            backend->rows[slot] = object;
            backend->row_kinds[slot] = kind;
            event_object = kind == CANOPUS_TARGET_ROW_SWITCH ?
#ifndef CANOPUS_MGR_BAND11
                get_trailing(object) : object;
            if (event_object == NULL) {
                return -1;
            }
            /* Switches register on the trailing object for LV_EVENT_ALL and
             * toggle on VALUE_CHANGED, exactly like the stock VAS alarm switch
             * (sub_C661410); rows register on the row for LV_EVENT_CLICKED. */
            event_code = kind == CANOPUS_TARGET_ROW_SWITCH ?
                CANOPUS_TARGET_EVENT_ALL : CANOPUS_TARGET_EVENT_CLICKED;
            add_event(event_object, target_row_events[slot], event_code, NULL);
#else /* CANOPUS_MGR_BAND11 */
                *(void **)((uint8_t *)object + FW_LVX_LIST_TRAILING_OFFSET) : object;
            if (event_object == NULL) return -1;
            backend->row_event_objects[slot] = event_object;
            add_event(event_object, target_row_events[slot],
                      kind == CANOPUS_TARGET_ROW_SWITCH ?
                      CANOPUS_TARGET_EVENT_VALUE_CHANGED : CANOPUS_TARGET_EVENT_CLICKED,
                      NULL);
#endif /* CANOPUS_MGR_BAND11 */
            backend->row_count++;
        }
        {
#ifndef CANOPUS_MGR_BAND11
            uint8_t selected = kind == CANOPUS_TARGET_ROW_SWITCH ?
                ((node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED | CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u ? 1u : 0u) :
                1u;
            (void)update_row(object, NULL, primary, secondary, 0, selected);
#else /* CANOPUS_MGR_BAND11 */
            uint8_t selected = kind != CANOPUS_TARGET_ROW_SWITCH ||
                (node->flags & (CANOPUS_UI_NODE_FLAG_CHECKED | CANOPUS_UI_NODE_FLAG_SELECTED)) != 0u;
            update_row(object, NULL, primary,
                       node->secondary_len ? snapshot->strings + node->secondary_off : NULL,
                       0, selected);
#endif /* CANOPUS_MGR_BAND11 */
        }
#ifndef CANOPUS_MGR_BAND11
        set_hidden(object, 0u);
#else /* CANOPUS_MGR_BAND11 */
        target_set_hidden(object, 0u);
#endif /* CANOPUS_MGR_BAND11 */
        if (previous == NULL) {
            align_to(object, backend->content_root,
                     CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
            first = object;
        } else {
            align_to(object, previous, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0,
                     gap);
        }
        previous = object;
#ifdef CANOPUS_MGR_BAND11
        backend->row_keys[slot] = node->key;
#endif /* CANOPUS_MGR_BAND11 */
        backend->bindings[slot].generation = snapshot->generation;
        backend->bindings[slot].key = node->key;
        backend->bindings[slot].event_id = node->event_id;
        used_mask |= UINT32_C(1) << (uint32_t)slot;
    }
    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] != NULL &&
            (used_mask & (UINT32_C(1) << i)) == 0u) {
#ifndef CANOPUS_MGR_BAND11
            set_hidden(backend->rows[i], 1u);
#else /* CANOPUS_MGR_BAND11 */
            target_set_hidden(backend->rows[i], 1u);
#endif /* CANOPUS_MGR_BAND11 */
            canopus_memset(&backend->bindings[i], 0,
                           sizeof(backend->bindings[i]));
        }
    }
    for (i = (uint16_t)label_used; i < CANOPUS_TARGET_UI_MAX_LABELS; i++) {
#ifndef CANOPUS_MGR_BAND11
        if (backend->labels[i] != NULL) set_hidden(backend->labels[i], 1u);
#else /* CANOPUS_MGR_BAND11 */
        if (backend->labels[i] != NULL) target_set_hidden(backend->labels[i], 1u);
#endif /* CANOPUS_MGR_BAND11 */
    }
    for (i = (uint16_t)image_used; i < CANOPUS_TARGET_UI_MAX_IMAGES; i++) {
#ifndef CANOPUS_MGR_BAND11
        if (backend->images[i] != NULL) set_hidden(backend->images[i], 1u);
#else /* CANOPUS_MGR_BAND11 */
        if (backend->images[i] != NULL) target_set_hidden(backend->images[i], 1u);
#endif /* CANOPUS_MGR_BAND11 */
    }
    for (i = (uint16_t)progress_used; i < CANOPUS_TARGET_UI_MAX_PROGRESS; i++) {
#ifndef CANOPUS_MGR_BAND11
        if (backend->progress[i] != NULL) set_hidden(backend->progress[i], 1u);
#else /* CANOPUS_MGR_BAND11 */
        if (backend->progress[i] != NULL) target_set_hidden(backend->progress[i], 1u);
#endif /* CANOPUS_MGR_BAND11 */
    }
    backend->rendered_generation = snapshot->generation;
    canopus_manager_target_record.list_row = (uintptr_t)first;
    return 0;
}

static const struct canopus_ui_backend_v1 target_ui_backend_api = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    target_ui_apply,
};

static struct canopus_target_page_context *context_for_native(
    struct canopus_manager_native_v1 *native)
{
    uint32_t i;

    if (native == NULL) {
        return NULL;
    }
    for (i = 0; i < CANOPUS_TARGET_PAGE_COUNT; i++) {
        if (&manager_pages[i].native == native) {
            return &manager_pages[i];
        }
    }
    return NULL;
}

static struct canopus_target_page_context *context_for_page(
    const struct canopus_target_page *page)
{
    uint32_t i;

    if (page == NULL) {
        return NULL;
    }
    for (i = 0; i < CANOPUS_TARGET_PAGE_COUNT; i++) {
        if (&manager_pages_desc[i] == page) {
            return &manager_pages[i];
        }
    }
    return NULL;
}

static uint32_t target_page_key(uint32_t page_index)
{
    return ((uint32_t)CANOPUS_MANAGER_TARGET_APP_ID << 16) | page_index;
}

static int target_select_page_view(
    const struct canopus_target_page_context *context)
{
    uint32_t selected;

    if (context == NULL) {
        return -1;
    }
    switch (context->backend.page_index) {
    case CANOPUS_TARGET_PAGE_OVERVIEW:
        return canopus_manager_goto(&manager_model,
                                    CANOPUS_MANAGER_VIEW_DEVICE, 0u);
    case CANOPUS_TARGET_PAGE_MODULES:
        return canopus_manager_goto(&manager_model,
                                    CANOPUS_MANAGER_VIEW_MODULE_LIST, 0u);
    case CANOPUS_TARGET_PAGE_DETAIL:
        selected = manager_model.selected;
        if (selected >= manager_model.module_count) {
            return -1;
        }
        return canopus_manager_goto(&manager_model,
                                    CANOPUS_MANAGER_VIEW_MODULE_DETAIL,
                                    selected);
    default:
        return -1;
    }
}

/* Re-read the device model after an operation so the page immediately shows
 * the committed pending state instead of the pre-operation snapshot. */
static int32_t target_refresh(void *cookie)
{
    struct canopus_target_page_context *context =
        (struct canopus_target_page_context *)cookie;
    if (context == NULL) {
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    if (target_refresh_model() != 0) {
        return CANOPUS_UI_ERR_STATE;
    }
    return target_select_page_view(context) == 0
               ? CANOPUS_UI_OK : CANOPUS_UI_ERR_STATE;
}

/* Routes a semantic view change to the real firmware page stack. Forward
 * routes push the target page through the stock page_goto transition; backward
 * routes finish the source page so the paused page below it resumes. */
static int32_t target_route(void *cookie,
                            struct canopus_manager_native_v1 *native,
                            uint32_t route)
{
    typedef int32_t (*page_goto_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
#ifndef CANOPUS_MGR_BAND11
    typedef int32_t (*page_finish_fn)(void *);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*page_finish_fn)(uint32_t);
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_page_context *source;
    page_goto_fn page_goto = (page_goto_fn)(uintptr_t)FW_ACTIVITY_NAVIGATE;
    page_finish_fn page_finish =
        (page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH;
    uint32_t target_page;
    uint32_t source_page;

    (void)cookie;
    switch (route) {
    case CANOPUS_MANAGER_ROUTE_OVERVIEW:
        target_page = CANOPUS_TARGET_PAGE_OVERVIEW;
        break;
    case CANOPUS_MANAGER_ROUTE_MODULES:
        target_page = CANOPUS_TARGET_PAGE_MODULES;
        break;
    case CANOPUS_MANAGER_ROUTE_MODULE_DETAIL:
        target_page = CANOPUS_TARGET_PAGE_DETAIL;
        manager_pending_detail = manager_model.selected + 1u;
        break;
    default:
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    source = context_for_native(native);
    if (source == NULL || !source->active || !source->interactive) {
        return CANOPUS_UI_ERR_STATE;
    }
    source_page = (uint32_t)source->backend.page_index;
    if (target_page == source_page) {
        return canopus_manager_native_render(native);
    }
    source->interactive = 0u;
    if (target_page < source_page) {
#ifndef CANOPUS_MGR_BAND11
        (void)page_finish(&manager_pages_desc[source_page]);
#else /* CANOPUS_MGR_BAND11 */
        (void)page_finish(target_page_key(source_page));
#endif /* CANOPUS_MGR_BAND11 */
        return CANOPUS_UI_OK;
    }
    (void)page_goto(target_page_key(target_page),
                    target_page == CANOPUS_TARGET_PAGE_DETAIL
                        ? manager_pending_detail : 0u,
                    0u, 0u);
    /* A push pauses the source. Rendering the destination snapshot into that
     * paused root caused the old two-tap/mixed-content defect. */
    return CANOPUS_UI_OK;
}

static int manager_page_on_create(struct canopus_target_page *page,
                                  void *root, void *start_data)
{
    struct canopus_target_page_context *context;
    uint32_t requested_detail = 0u;
    int32_t rc;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    if ((uint32_t)page->page_id == CANOPUS_TARGET_PAGE_DETAIL) {
        requested_detail = (uint32_t)(uintptr_t)start_data;
        if (requested_detail == 0u) {
            requested_detail = manager_pending_detail;
        }
    }
    canopus_manager_target_record.create_count += 1u;
    canopus_manager_target_record.root_object = (uintptr_t)root;
    canopus_memset(&context->backend, 0, sizeof(context->backend));
    context->backend.root = root;
    context->backend.firmware_page = page;
    context->backend.page_index = (uint8_t)page->page_id;
#ifndef CANOPUS_MGR_BAND11
    /* This runs on the regular page-owner task, after the supervisor's own
     * `insmod` constructor has returned. Ignore a restore error here so the
     * Manager remains available to display and repair a failed module slot. */
#else /* CANOPUS_MGR_BAND11 */
    /* Restore only after the staged Supervisor constructor returns;
     * nested module loads need the regular page-owner task's stack. */
#endif /* CANOPUS_MGR_BAND11 */
    (void)canopus_supervisor_restore_after_boot();
    if (!manager_session_ready) {
        rc = canopus_client_init(&manager_client, &target_device_io, NULL);
        if (rc != CANOPUS_CLIENT_OK ||
            canopus_client_open(&manager_client) != CANOPUS_CLIENT_OK) {
            return -1;
        }
        canopus_manager_init(&manager_model, canopus_client_transport,
                             &manager_client);
        canopus_manager_set_identity(
            &manager_model, CANOPUS_TARGET_ID, CANOPUS_TARGET_FIRMWARE_VERSION,
            CANOPUS_TARGET_FIRMWARE_BUILD, 1u);
        if (target_refresh_model() != 0) {
            (void)canopus_client_close(&manager_client);
            manager_client.fd = -1;
            return -1;
        }
        manager_session_ready = 1u;
    }
    if (context->backend.page_index == CANOPUS_TARGET_PAGE_DETAIL &&
        requested_detail != 0u) {
        uint32_t selected = requested_detail - 1u;
        if (selected >= manager_model.module_count) {
            return -1;
        }
        manager_model.selected = selected;
        canopus_manager_target_record.detail_selected = selected;
    }
    if (target_select_page_view(context) != 0) {
        return -1;
    }
    if (context->backend.page_index == CANOPUS_TARGET_PAGE_DETAIL) {
        manager_pending_detail = 0u;
    }
    rc = canopus_manager_native_init(&context->native, &manager_model,
                                     &target_ui_backend_api, &context->backend);
    if (rc != CANOPUS_UI_OK) {
        return -1;
    }
    canopus_manager_native_set_router(&context->native, target_route, NULL);
    canopus_manager_native_set_refresh(&context->native, target_refresh,
                                       context);
    if (!context->active) {
        context->active = 1u;
        manager_active_pages++;
    }
    context->interactive = 1u;
    return 0;
}

static int manager_page_on_resume(struct canopus_target_page *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL || !context->active) {
        return -1;
    }
    canopus_manager_target_record.resume_count += 1u;
    context->interactive = 1u;
    if (manager_session_ready && target_refresh_model() == 0 &&
        target_select_page_view(context) == 0) {
        (void)canopus_manager_native_render(&context->native);
    }
    return 0;
}

static int manager_page_on_pause(struct canopus_target_page *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    context->interactive = 0u;
    canopus_manager_target_record.pause_count += 1u;
    return 0;
}

#ifndef CANOPUS_MGR_BAND11
static int manager_page_on_destroy(struct canopus_target_page *page)
#else /* CANOPUS_MGR_BAND11 */
static int manager_page_on_ui_destroy(struct canopus_target_page *page)
#endif /* CANOPUS_MGR_BAND11 */
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    canopus_manager_target_record.destroy_count += 1u;
    canopus_manager_target_record.list_row = 0u;
    canopus_manager_target_record.root_object = 0u;
    if (context->active) {
#ifndef CANOPUS_MGR_BAND11
        target_dialog_dismiss(&context->backend);
#endif /* CANOPUS_MGR_BAND11 */
        context->active = 0u;
        if (manager_active_pages > 0u) {
            manager_active_pages--;
        }
    }
    canopus_memset(&context->backend, 0, sizeof(context->backend));
    canopus_memset(&context->native, 0, sizeof(context->native));
    if (manager_active_pages == 0u && manager_session_ready) {
        if (manager_client.fd >= 0) {
            (void)canopus_client_close(&manager_client);
        }
        manager_session_ready = 0u;
        canopus_memset(&manager_model, 0, sizeof(manager_model));
        canopus_memset(&manager_client, 0, sizeof(manager_client));
        manager_client.fd = -1;
    }
    return 0;
}

#ifndef CANOPUS_MGR_BAND11
static const struct firmware_app_descriptor manager_app = {
#else /* CANOPUS_MGR_BAND11 */
static struct firmware_app_descriptor manager_app = {
#endif /* CANOPUS_MGR_BAND11 */
    .package_name = package_name,
    .launcher_icon_resource = launcher_icon,
    .app_id = CANOPUS_MANAGER_TARGET_APP_ID,
    .launcher_metadata_callback = manager_display_name,
};

#ifdef CANOPUS_MGR_BAND11
/* The reminder UI reads user_data->focus_version even for ordinary messages.
 * Stock notify_set_user_data allocates this 16-byte object (not just a callback
 * cookie). The list/reminder clones borrow it. Keep a separate writable,
 * boot-resident context for each notice; no stock phone/focus callbacks own it.
 * Evidence: EVID-NOTIFICATION-4139-002, crash3 PC 0x0c55fe06. */
struct band11_notification_context {
    void *phone_data;
    void *focus_v1;
    void *focus_v2;
    uint8_t phone_active;
    uint8_t focus_version;
    uint8_t reserved[2];
};
_Static_assert(sizeof(struct band11_notification_context) == 16,
               "notification context size");
_Static_assert(offsetof(struct band11_notification_context, focus_version) == 13,
               "notification focus version offset");
static struct band11_notification_context loaded_notification_context;
static struct band11_notification_context module_notification_context;

#endif /* CANOPUS_MGR_BAND11 */
static const struct firmware_notification_message loaded_notification = {
    .message_id = UINT64_C(0x43414E4F50555301),
#ifndef CANOPUS_MGR_BAND11
    .title = notification_title,
    .source = notification_title,
    .body = notification_body,
    .small_icon_path = notification_icon,
    .large_icon_path = notification_icon,
#else /* CANOPUS_MGR_BAND11 */
    .title = "Canopus",
    .source = "Canopus",
    .body = "Canopus 已加载！尽情享受吧～",
    .small_icon_path = (void *)launcher_icon,
    .large_icon_path = (void *)launcher_icon,
#endif /* CANOPUS_MGR_BAND11 */
    .start_reminder = 1u,
#ifdef CANOPUS_MGR_BAND11
    .callback_data = &loaded_notification_context,
#endif /* CANOPUS_MGR_BAND11 */
};

static const struct firmware_notification_message module_notification = {
    .message_id = UINT64_C(0x43414E4F50555302),
#ifndef CANOPUS_MGR_BAND11
    .title = notification_title,
    .source = notification_title,
    .body = module_notification_body,
    .small_icon_path = notification_icon,
    .large_icon_path = notification_icon,
#else /* CANOPUS_MGR_BAND11 */
    .title = "Canopus",
    .source = "Canopus",
    .body = "新模块已安装但处于禁用状态。打开 Canopus 管理器即可启用。",
    .small_icon_path = (void *)launcher_icon,
    .large_icon_path = (void *)launcher_icon,
#endif /* CANOPUS_MGR_BAND11 */
    .start_reminder = 1u,
#ifdef CANOPUS_MGR_BAND11
    .callback_data = &module_notification_context,
#endif /* CANOPUS_MGR_BAND11 */
};

#ifndef CANOPUS_MGR_BAND11
int canopus_manager_native_notify_module_installed(void)
#else /* CANOPUS_MGR_BAND11 */
static int target_notify(const struct firmware_notification_message *message)
#endif /* CANOPUS_MGR_BAND11 */
{
#ifndef CANOPUS_MGR_BAND11
    typedef int (*notification_insert_fn)(
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*notification_insert_fn)(
#endif /* CANOPUS_MGR_BAND11 */
        const struct firmware_notification_message *);
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;

    if (identity_guard() != 0) {
        return -1;
    }
#ifndef CANOPUS_MGR_BAND11
    return notification_insert(&module_notification);
#else /* CANOPUS_MGR_BAND11 */
    /* The stock entry returns free() residue, not a delivery result. */
    notification_insert(message);
    return 0;
#endif /* CANOPUS_MGR_BAND11 */
}

#ifdef CANOPUS_MGR_BAND11
int canopus_manager_native_notify_module_installed(void)
{
    return target_notify(&module_notification);
}

/* Each posted notice borrows its own zeroed context (see above); a small ring
 * keeps the most recent ones alive for the reminder UI. */
#define BAND11_NOTIFY_RING 4u
static struct band11_notification_context notify_contexts[BAND11_NOTIFY_RING];
static uint32_t notify_counter;

#endif /* CANOPUS_MGR_BAND11 */
int canopus_manager_native_notify(const char *source, const char *title, const char *body)
{
#ifndef CANOPUS_MGR_BAND11
    typedef int (*notification_insert_fn)(
        const struct firmware_notification_message *);
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;
    static uint32_t counter;
#endif /* CANOPUS_MGR_BAND11 */
    struct firmware_notification_message message;
#ifdef CANOPUS_MGR_BAND11
    uint32_t slot = notify_counter % BAND11_NOTIFY_RING;
#endif /* CANOPUS_MGR_BAND11 */

#ifndef CANOPUS_MGR_BAND11
    if (identity_guard() != 0) {
        return -1;
    }
#endif /* CANOPUS_MGR_BAND11 */
    canopus_memset(&message, 0, sizeof(message));
#ifndef CANOPUS_MGR_BAND11
    message.small_icon_path = (void *)notification_icon;
    message.large_icon_path = (void *)notification_icon;
#else /* CANOPUS_MGR_BAND11 */
    message.small_icon_path = (void *)launcher_icon;
    message.large_icon_path = (void *)launcher_icon;
#endif /* CANOPUS_MGR_BAND11 */
    message.start_reminder = 1u;
#ifndef CANOPUS_MGR_BAND11
    message.message_id = UINT64_C(0x43414E4F50555310) + (counter++ & 0xffu);
#else /* CANOPUS_MGR_BAND11 */

    canopus_memset(&notify_contexts[slot], 0, sizeof(notify_contexts[slot]));
    message.message_id = UINT64_C(0x43414E4F50555310) + (notify_counter++ & 0xffu);
#endif /* CANOPUS_MGR_BAND11 */
    message.title = (void *)title;
    message.source = (void *)source;
    message.body = (void *)body;
#ifndef CANOPUS_MGR_BAND11
    return notification_insert(&message) < 0 ? -1 : 0;
#else /* CANOPUS_MGR_BAND11 */
    message.callback_data = &notify_contexts[slot];
    return target_notify(&message);
#endif /* CANOPUS_MGR_BAND11 */
}

static void __attribute__((constructor, used)) canopus_manager_target_init(void)
{
    canopus_manager_target_record.magic = CANOPUS_MANAGER_TARGET_MAGIC;
    canopus_manager_target_record.build_id = CANOPUS_MANAGER_TARGET_BUILD_ID;
    canopus_manager_target_record.identity_result = identity_guard();
}

static void target_descriptor_init(uint32_t page_index, const char *name,
                                   uint16_t page_id)
{
    if (page_index >= CANOPUS_TARGET_PAGE_COUNT) {
        return;
    }
    canopus_memset(&manager_pages_desc[page_index], 0,
                   sizeof(manager_pages_desc[page_index]));
    manager_pages_desc[page_index].page_name = name;
    manager_pages_desc[page_index].page_id = page_id;
    manager_pages_desc[page_index].app_id = CANOPUS_MANAGER_TARGET_APP_ID;
    manager_pages_desc[page_index].on_signal = manager_on_signal;
    manager_pages_desc[page_index].on_create = manager_page_on_create;
    manager_pages_desc[page_index].on_resume = manager_page_on_resume;
    manager_pages_desc[page_index].on_pause = manager_page_on_pause;
#ifndef CANOPUS_MGR_BAND11
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 */
    manager_pages_desc[page_index].on_ui_destroy = manager_page_on_ui_destroy;
#endif /* CANOPUS_MGR_BAND11 */
}

int canopus_manager_native_install(void)
{
#ifndef CANOPUS_MGR_BAND11
    typedef int (*app_install_fn)(const struct firmware_app_descriptor *,
#else /* CANOPUS_MGR_BAND11 */
    typedef int (*app_install_fn)(struct firmware_app_descriptor *,
#endif /* CANOPUS_MGR_BAND11 */
                                  struct canopus_target_page *const *,
                                  uint32_t);
    typedef void *(*app_lookup_fn)(uint16_t);
#ifndef CANOPUS_MGR_BAND11
    typedef int (*launcher_add_fn)(uint16_t);
    typedef int (*notification_insert_fn)(
        const struct firmware_notification_message *);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*launcher_add_fn)(uint16_t);
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_page *pages[CANOPUS_TARGET_PAGE_COUNT];
    app_install_fn app_install = (app_install_fn)(uintptr_t)FW_APP_INSTALL;
    app_lookup_fn app_lookup = (app_lookup_fn)(uintptr_t)FW_APP_LOOKUP;
#ifndef CANOPUS_MGR_BAND11
    launcher_add_fn launcher_add =
        (launcher_add_fn)(uintptr_t)FW_LAUNCHER_ADD;
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;
#else /* CANOPUS_MGR_BAND11 */
    launcher_add_fn launcher_add = (launcher_add_fn)(uintptr_t)FW_APP_LAUNCHER_ADD;
#endif /* CANOPUS_MGR_BAND11 */
    void *installed_app;

    if (canopus_manager_target_record.identity_result != 0) {
        return canopus_manager_target_record.identity_result;
    }
    installed_app = app_lookup(CANOPUS_MANAGER_TARGET_APP_ID);
    if (installed_app != NULL) {
        const char *installed_package =
#ifndef CANOPUS_MGR_BAND11
            *(const char **)((uint8_t *)installed_app + 8u);
#else /* CANOPUS_MGR_BAND11 */
            *(const char **)((uint8_t *)installed_app + 12u);
#endif /* CANOPUS_MGR_BAND11 */
        return installed_package != NULL &&
                       strings_differ(installed_package, package_name) == 0
                   ? 0
                   : -101;
    }

    target_descriptor_init(CANOPUS_TARGET_PAGE_OVERVIEW, page_name_overview,
                           0u);
    target_descriptor_init(CANOPUS_TARGET_PAGE_MODULES, page_name_modules, 1u);
    target_descriptor_init(CANOPUS_TARGET_PAGE_DETAIL, page_name_detail, 2u);
    pages[0] = &manager_pages_desc[0];
    pages[1] = &manager_pages_desc[1];
    pages[2] = &manager_pages_desc[2];
    canopus_manager_target_record.app_install_result =
        app_install(&manager_app, pages, CANOPUS_TARGET_PAGE_COUNT);
#ifndef CANOPUS_MGR_BAND11
    if (app_lookup(CANOPUS_MANAGER_TARGET_APP_ID) == NULL) {
#else /* CANOPUS_MGR_BAND11 */
    installed_app = app_lookup(CANOPUS_MANAGER_TARGET_APP_ID);
    if (installed_app == NULL) {
#endif /* CANOPUS_MGR_BAND11 */
        canopus_manager_target_record.app_install_result = -100;
        return -100;
    }
#ifndef CANOPUS_MGR_BAND11
    canopus_manager_target_record.launcher_add_result =
        launcher_add(CANOPUS_MANAGER_TARGET_APP_ID);
#else /* CANOPUS_MGR_BAND11 */
    launcher_add(CANOPUS_MANAGER_TARGET_APP_ID);
    canopus_manager_target_record.launcher_add_result = 0;
#endif /* CANOPUS_MGR_BAND11 */
    canopus_manager_target_record.notification_result =
#ifndef CANOPUS_MGR_BAND11
        notification_insert(&loaded_notification);
#else /* CANOPUS_MGR_BAND11 */
        target_notify(&loaded_notification);
#endif /* CANOPUS_MGR_BAND11 */
    return 0;
}
