/*
 * Exact-target native Manager registration and stock-LVX semantic UI backend.
 *
 * This deliberately uses fixed-address veneers for firmware 3.101.030 and is
 * resident-first: its constructor only records target identity. The supervisor
 * invokes the exported install entry from a /dev/canopus write made by miwear,
 * then install registers one writable page descriptor and one launcher record.
 * The page maps the portable Manager snapshot to stock LVX rows; firmware widget
 * pointers remain private to this target backend. Device testing must treat
 * reboot as the reliable recovery path until concurrent callback drain is proven.
 */
#include <stddef.h>
#include <stdint.h>

#include "canopus_manager_native_probe.h"
#include "canopus_client.h"
#include "canopus_manager_native.h"
#include "canopus_memory.h"

#define CANOPUS_PROBE_APP_ID UINT16_C(0x00CA)
#define CANOPUS_PROBE_PAGE_ID UINT16_C(0)
#define CANOPUS_PROBE_MAGIC UINT32_C(0x434E5031) /* "CNP1" */

#define FW_VERSION_ADDRESS UINT32_C(0x0C0C0810)
#define FW_BUILD_ADDRESS UINT32_C(0x0C0C0850)
#define FW_APP_LOOKUP UINT32_C(0x0CA50FD5)
#define FW_APP_INSTALL UINT32_C(0x0CA519AD)
#define FW_LAUNCHER_ADD UINT32_C(0x0C4F2BDD)
#define FW_LVX_LIST_ROW_CREATE UINT32_C(0x0C52B235)
#define FW_LVX_LIST_ROW_UPDATE UINT32_C(0x0C4A7BD1)
#define FW_LVX_LIST_ROW_TRAILING UINT32_C(0x0C4A7F2D)
#define FW_LVX_LABEL_CREATE UINT32_C(0x0C588339)
#define FW_LVX_LABEL_SET_TEXT UINT32_C(0x0C588849)
#define FW_LVX_CONTENT_CREATE UINT32_C(0x0CA4E8E9)
#define FW_LVX_OBJECT_SET_SIZE UINT32_C(0x0C587EF9)
#define FW_LVX_OBJECT_ALIGN UINT32_C(0x0C5880A9)
#define FW_LVX_PAGE_TITLE_CREATE UINT32_C(0x0C4A9991)
#define FW_LVX_STYLE_APPLY UINT32_C(0x0C49EA99)
#define FW_STYLE_MISANS_DEMIBOLD_32 UINT32_C(0x2010A02C)
#define FW_LVX_EVENT_ADD UINT32_C(0x0C5882B9)
#define FW_LVX_EVENT_GET_USER_DATA UINT32_C(0x0C588601)
#define FW_LVX_EVENT_GET_CODE UINT32_C(0x0C5886D1)
#define FW_LVX_SET_HIDDEN UINT32_C(0x0C588459)
#define FW_LVX_ALIGN_TO UINT32_C(0x0C588BE9)
#define FW_NUTTX_OPEN UINT32_C(0x0C1C15B1)
#define FW_NUTTX_CLOSE UINT32_C(0x0C1AAB71)
#define FW_NUTTX_READ UINT32_C(0x0C1C1E25)
#define FW_NUTTX_WRITE UINT32_C(0x0C1C31C9)
#define FW_NOTIFICATION_INSERT UINT32_C(0x0CA81F11)
#define FW_ACTIVITY_NAVIGATE UINT32_C(0x0CA539F9)
#define FW_ACTIVITY_FINISH UINT32_C(0x0CA53089)
#define FW_LVX_MSGBOX_CREATE UINT32_C(0x0C4A93A5)
#define FW_LVX_MSGBOX_SET_CONTENT UINT32_C(0x0C4A93FD)
#define FW_LVX_OBJECT_DELETE UINT32_C(0x0C5888F1)

#define CANOPUS_TARGET_PAGE_COUNT 3u
#define CANOPUS_TARGET_PAGE_OVERVIEW 0u
#define CANOPUS_TARGET_PAGE_MODULES 1u
#define CANOPUS_TARGET_PAGE_DETAIL 2u
#define CANOPUS_TARGET_UI_MAX_ROWS 25u
#define CANOPUS_TARGET_UI_MAX_LABELS 6u
#define CANOPUS_TARGET_MODULE_FLAG_SIGNATURE_OK (1u << 0)
#define CANOPUS_TARGET_ROW_STATUS 1u
#define CANOPUS_TARGET_ROW_ACTION 2u
#define CANOPUS_TARGET_ROW_SWITCH 3u
#define CANOPUS_TARGET_TRAILING_NONE 0u
#define CANOPUS_TARGET_TRAILING_SWITCH 1u
#define CANOPUS_TARGET_TRAILING_FORWARD 3u
#define CANOPUS_TARGET_ALIGN_TOP_MID 2u
#define CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID 14u
#define CANOPUS_TARGET_CONTENT_TOP_OFFSET 56
#define CANOPUS_TARGET_CONTENT_WIDTH 336
#define CANOPUS_TARGET_CONTENT_HEIGHT 424
#define CANOPUS_TARGET_ROW_GAP 8
#define CANOPUS_TARGET_EVENT_ALL 0u
#define CANOPUS_TARGET_EVENT_CLICKED 7u
#define CANOPUS_TARGET_EVENT_VALUE_CHANGED 30u

struct firmware_page_descriptor;

typedef int (*page_signal_fn)(struct firmware_page_descriptor *, uint32_t,
                              void *);
typedef int (*page_create_fn)(struct firmware_page_descriptor *, void *,
                              void *);
typedef int (*page_lifecycle_fn)(struct firmware_page_descriptor *);
typedef int (*page_foreground_data_fn)(struct firmware_page_descriptor *,
                                       void *);

struct firmware_page_descriptor {
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
    page_lifecycle_fn on_pause;               /* +0x58 */
    page_lifecycle_fn on_destroy;             /* +0x5c */
    page_lifecycle_fn on_ui_destroy;          /* +0x60 */
    void *extension_callback_100;             /* +0x64 */
    void *extension_callback_104;             /* +0x68 */
    void *extension_callback_108;             /* +0x6c */
    uint32_t _tail_70;                        /* +0x70 */
};

struct firmware_app_descriptor {
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
};

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

struct canopus_native_probe_record {
    uint32_t magic;
    int32_t identity_result;
    int32_t app_install_result;
    int32_t launcher_add_result;
    int32_t notification_result;
    uint32_t create_count;
    uint32_t resume_count;
    uint32_t pause_count;
    uint32_t destroy_count;
    uintptr_t root_object;
    uintptr_t list_row;
};

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
    void *labels[CANOPUS_TARGET_UI_MAX_LABELS];
    uint8_t row_kinds[CANOPUS_TARGET_UI_MAX_ROWS];
    struct canopus_target_ui_binding bindings[CANOPUS_TARGET_UI_MAX_ROWS];
    struct firmware_page_descriptor *firmware_page;
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
static struct firmware_page_descriptor
    manager_pages_desc[CANOPUS_TARGET_PAGE_COUNT];
static uint32_t manager_active_pages;
static uint8_t manager_session_ready;

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

_Static_assert(CANOPUS_PROBE_APP_ID <= UINT16_C(0x00FF),
               "system launch animation requires an 8-bit app id");
_Static_assert(sizeof(struct firmware_page_descriptor) == 116,
               "firmware page descriptor size");
_Static_assert(offsetof(struct firmware_page_descriptor, page_name) == 16,
               "page name offset");
_Static_assert(offsetof(struct firmware_page_descriptor, root_object) == 48,
               "page root offset");
_Static_assert(offsetof(struct firmware_page_descriptor, on_create) == 76,
               "page create offset");
_Static_assert(offsetof(struct firmware_page_descriptor, on_destroy) == 92,
               "page destroy offset");
_Static_assert(sizeof(struct firmware_app_descriptor) == 64,
               "firmware app descriptor size");
_Static_assert(sizeof(struct firmware_notification_message) == 88,
               "firmware notification message size");
_Static_assert(offsetof(struct firmware_notification_message, title) == 12,
               "notification title offset");
_Static_assert(offsetof(struct firmware_notification_message, small_icon_path) == 28,
               "notification icon offset");
_Static_assert(offsetof(struct firmware_notification_message, start_reminder) == 80,
               "notification reminder offset");

static const char package_name[] = "com.canopus.manager";
static const char page_name_overview[] = "main";
static const char page_name_modules[] = "modules";
static const char page_name_detail[] = "module_detail";
static const char display_name[] = "Canopus Manager";
static const char empty_detail[] = "";
static const char notification_title[] = "Canopus";
static const char notification_body[] = "Canpous Loaded! Just ENJOY~";
static const char module_notification_body[] =
    "A new module was installed disabled. Open Canopus Manager to enable it.";
/* The watchface bootstrap stages the first-frame PNG at this stable path. */
static const char notification_icon[] = "/data/canopus/manager_loaded.png";
/* Reuse a stock, proven launcher asset for the first destructive device probe. */
static const char launcher_icon[] = "/resource/app/launcher/flashlight.bin";

__attribute__((used, visibility("default"), section(".data.canopus_probe")))
volatile struct canopus_native_probe_record canopus_native_probe_record;

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
    const char *version = (const char *)(uintptr_t)FW_VERSION_ADDRESS;
    const char *build = (const char *)(uintptr_t)FW_BUILD_ADDRESS;

    if (strings_differ(version, "3.101.030") != 0) {
        return -1;
    }
    if (strings_differ(build, "CONBINE_LTALM078_T3.101.030_06011854") != 0) {
        return -2;
    }
    return 0;
}

static const char *manager_display_name(void)
{
    return display_name;
}

static int manager_on_signal(struct firmware_page_descriptor *page,
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
        module.version = source.version;
        module.activation_error = source.activation_error;
        module.signature_ok =
            (source.flags & CANOPUS_TARGET_MODULE_FLAG_SIGNATURE_OK) != 0u;
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

static void target_row_event(void *event)
{
    typedef uintptr_t (*get_user_data_fn)(void *);
    typedef uint32_t (*get_event_code_fn)(void *);
    get_user_data_fn get_user_data =
        (get_user_data_fn)(uintptr_t)FW_LVX_EVENT_GET_USER_DATA;
    get_event_code_fn get_code =
        (get_event_code_fn)(uintptr_t)FW_LVX_EVENT_GET_CODE;
    uint32_t code = get_code(event);
    uintptr_t encoded = get_user_data(event);
    uint32_t page_index = (uint32_t)(encoded >> 8);
    uint32_t row_index = (uint32_t)(encoded & UINT32_C(0xFF));
    struct canopus_target_page_context *context;
    struct canopus_target_ui_binding *binding;
    int32_t rc;

    if (page_index >= CANOPUS_TARGET_PAGE_COUNT ||
        row_index >= CANOPUS_TARGET_UI_MAX_ROWS) {
        return;
    }
    context = &manager_pages[page_index];
    if (!context->active || !context->interactive) {
        return;
    }
    /* Stock switch rows register for LV_EVENT_ALL on the switch and act only
     * on LV_EVENT_VALUE_CHANGED (sub_C661410/sub_C6613C0); action/status rows
     * register for LV_EVENT_CLICKED on the row (sub_C52C228). */
    if (context->backend.row_kinds[row_index] == CANOPUS_TARGET_ROW_SWITCH) {
        if (code != CANOPUS_TARGET_EVENT_VALUE_CHANGED) {
            return;
        }
    } else if (code != CANOPUS_TARGET_EVENT_CLICKED) {
        return;
    }
    binding = &context->backend.bindings[row_index];
    if (binding->event_id == 0u) {
        return;
    }
    rc = canopus_ui_dispatch_event(&context->native.ui,
                                   binding->generation,
                                   binding->key,
                                   binding->event_id);
    if (rc == CANOPUS_UI_ERR_DISABLED) {
        /* A disabled stock switch may optimistically animate before its event.
         * Reapplying the authoritative snapshot immediately restores its state. */
        (void)canopus_manager_native_render(&context->native);
    }
}

static uint8_t target_row_kind(uint16_t node_type)
{
    if (node_type == CANOPUS_UI_NODE_SWITCH_ROW) {
        return CANOPUS_TARGET_ROW_SWITCH;
    }
    if (node_type == CANOPUS_UI_NODE_BUTTON ||
        node_type == CANOPUS_UI_NODE_ACTION_ROW) {
        return CANOPUS_TARGET_ROW_ACTION;
    }
    return CANOPUS_TARGET_ROW_STATUS;
}

static int target_find_row(struct canopus_target_ui_backend *backend,
                           uint8_t kind, uint32_t used_mask)
{
    uint32_t i;
    int empty = -1;

    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] == NULL) {
            if (empty < 0) {
                empty = (int)i;
            }
        } else if (backend->row_kinds[i] == kind &&
                   (used_mask & (UINT32_C(1) << i)) == 0u) {
            return (int)i;
        }
    }
    return empty;
}

static int target_find_confirmation(
    const struct canopus_ui_snapshot_v1 *snapshot,
    const struct canopus_ui_node_v1 **message,
    const struct canopus_ui_node_v1 **confirm,
    const struct canopus_ui_node_v1 **cancel)
{
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
}

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
    if (backend->content_root == NULL || backend->firmware_page == NULL) return -1;
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
    backend->dialog.buttons[0].text = "Cancel";
    backend->dialog.buttons[0].user_data = cancel_binding;
    backend->dialog.buttons[1].callback = target_dialog_event;
    backend->dialog.buttons[1].text = "Confirm";
    backend->dialog.buttons[1].user_data = confirm_binding;
    backend->dialog.native_box = create_box(backend->content_root,
                                             backend->firmware_page);
    if (backend->dialog.native_box == NULL) return -1;
    backend->dialog.active = 1u;
    text = snapshot->strings + message->primary_off;
    set_content(backend->dialog.native_box, 2u, NULL, text, NULL,
                backend->dialog.buttons, 2u);
    return 0;
}

static int32_t target_ui_apply(
    void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    typedef void *(*create_row_fn)(void *, const char *, const char *, uint8_t);
    typedef int (*update_row_fn)(void *, const char *, const char *,
                                 const char *, int32_t, uint8_t);
    typedef void *(*get_trailing_fn)(void *);
    typedef void *(*create_label_fn)(void *);
    typedef void (*set_label_text_fn)(void *, const char *);
    typedef void *(*create_content_fn)(void *);
    typedef void (*set_size_fn)(void *, int32_t, int32_t);
    typedef void (*align_fn)(void *, uint32_t, int32_t, int32_t);
    typedef void *(*create_page_title_fn)(void *, const char *, uint32_t,
                                          void (*)(void *), void *);
    typedef int (*apply_style_fn)(void *, const void *, uint32_t, uint32_t);
    typedef void (*add_event_fn)(void *, void (*)(void *), uint32_t, void *);
    typedef void (*set_hidden_fn)(void *, uint32_t);
    typedef void (*align_to_fn)(void *, void *, uint32_t, int32_t, int32_t);
    struct canopus_target_ui_backend *backend =
        (struct canopus_target_ui_backend *)cookie;
    create_row_fn create_row =
        (create_row_fn)(uintptr_t)FW_LVX_LIST_ROW_CREATE;
    update_row_fn update_row =
        (update_row_fn)(uintptr_t)FW_LVX_LIST_ROW_UPDATE;
    get_trailing_fn get_trailing =
        (get_trailing_fn)(uintptr_t)FW_LVX_LIST_ROW_TRAILING;
    create_label_fn create_label =
        (create_label_fn)(uintptr_t)FW_LVX_LABEL_CREATE;
    set_label_text_fn set_label_text =
        (set_label_text_fn)(uintptr_t)FW_LVX_LABEL_SET_TEXT;
    create_content_fn create_content =
        (create_content_fn)(uintptr_t)FW_LVX_CONTENT_CREATE;
    set_size_fn set_size = (set_size_fn)(uintptr_t)FW_LVX_OBJECT_SET_SIZE;
    align_fn align = (align_fn)(uintptr_t)FW_LVX_OBJECT_ALIGN;
    create_page_title_fn create_page_title =
        (create_page_title_fn)(uintptr_t)FW_LVX_PAGE_TITLE_CREATE;
    apply_style_fn apply_style =
        (apply_style_fn)(uintptr_t)FW_LVX_STYLE_APPLY;
    add_event_fn add_event = (add_event_fn)(uintptr_t)FW_LVX_EVENT_ADD;
    set_hidden_fn set_hidden = (set_hidden_fn)(uintptr_t)FW_LVX_SET_HIDDEN;
    align_to_fn align_to = (align_to_fn)(uintptr_t)FW_LVX_ALIGN_TO;
    uint16_t i;
    uint32_t visible_rows = 0u;
    uint32_t visible_labels = 0u;
    uint32_t used_mask = 0u;
    uint32_t label_used = 0u;
    void *previous = NULL;
    void *first = NULL;

    if (backend == NULL || backend->root == NULL || snapshot == NULL ||
        snapshot->node_count == 0u ||
        backend->page_index >= CANOPUS_TARGET_PAGE_COUNT) {
        return -1;
    }
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
    if (backend->content_root == NULL) {
        backend->content_root = create_content(backend->root);
        if (backend->content_root == NULL) {
            return -1;
        }
        set_size(backend->content_root, CANOPUS_TARGET_CONTENT_WIDTH,
                 CANOPUS_TARGET_CONTENT_HEIGHT);
        align(backend->content_root, CANOPUS_TARGET_ALIGN_TOP_MID, 0,
              CANOPUS_TARGET_CONTENT_TOP_OFFSET);
    }
    for (i = 0; i < snapshot->node_count; i++) {
        uint16_t type = snapshot->nodes[i].type;
        if (type == CANOPUS_UI_NODE_SECTION) {
            continue;
        }
        if (type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
            continue;
        }
        if (type == CANOPUS_UI_NODE_TEXT) {
            visible_labels++;
            continue;
        }
        if (type != CANOPUS_UI_NODE_STATUS_ROW &&
            type != CANOPUS_UI_NODE_BUTTON &&
            type != CANOPUS_UI_NODE_ACTION_ROW &&
            type != CANOPUS_UI_NODE_SWITCH_ROW) {
            return -1;
        }
        visible_rows++;
    }
    if (visible_labels > CANOPUS_TARGET_UI_MAX_LABELS ||
        visible_rows > CANOPUS_TARGET_UI_MAX_ROWS) {
        return -1;
    }

    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        const char *primary;
        const char *secondary = empty_detail;
        uint8_t kind;
        uint8_t trailing;
        int slot;
        void *object;
        int32_t gap = CANOPUS_TARGET_ROW_GAP;

        if (node->type == CANOPUS_UI_NODE_SECTION) {
            continue;
        }
        primary = snapshot->strings + node->primary_off;
        if (node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
            uint32_t title_mode = backend->page_index ==
                CANOPUS_TARGET_PAGE_OVERVIEW ? 0u : 1u;

            if (backend->page_title == NULL) {
                backend->page_title = create_page_title(
                    backend->root, primary, title_mode,
                    title_mode != 0u ? target_page_title_back : NULL,
                    title_mode != 0u ? (void *)&manager_pages[
                        backend->page_index] : NULL);
                if (backend->page_title == NULL) {
                    return -1;
                }
            }
            /* lvx_page_title_create owns the stock left title, right-side time,
             * optional back affordance, and its standard content inset. It is
             * intentionally created once per firmware page: updating its title
             * contract is not yet recovered. */
            set_hidden(backend->page_title, 0u);
            previous = backend->page_title;
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
            set_label_text(object, primary);
            if (snapshot->styles[i].text_style == CANOPUS_UI_TEXT_TITLE) {
                /* lvx_theme_default_init maps this exact-target style object to
                 * MiSans-Demibold at 32 px. sub_C49EA98 is the stock helper used
                 * by firmware labels to replace the inherited text style. */
                (void)apply_style(
                    object,
                    (const void *)(uintptr_t)FW_STYLE_MISANS_DEMIBOLD_32,
                    255u, 0u);
            }
            set_hidden(object, 0u);
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
        if (node->secondary_len != 0u) {
            secondary = snapshot->strings + node->secondary_off;
        }
        kind = target_row_kind(node->type);
        if (kind == CANOPUS_TARGET_ROW_ACTION) {
            trailing = CANOPUS_TARGET_TRAILING_FORWARD;
        } else if (kind == CANOPUS_TARGET_ROW_SWITCH) {
            trailing = CANOPUS_TARGET_TRAILING_SWITCH;
        } else {
            trailing = CANOPUS_TARGET_TRAILING_NONE;
        }
        slot = target_find_row(backend, kind, used_mask);
        if (slot < 0) {
            return -1;
        }
        object = backend->rows[slot];
        if (object == NULL) {
            void *event_object;
            uintptr_t event_cookie;
            uint32_t event_code;
            object = create_row(backend->content_root, primary, secondary, trailing);
            if (object == NULL) {
                return -1;
            }
            backend->rows[slot] = object;
            backend->row_kinds[slot] = kind;
            event_object = kind == CANOPUS_TARGET_ROW_SWITCH ?
                get_trailing(object) : object;
            if (event_object == NULL) {
                return -1;
            }
            /* Switches register on the trailing object for LV_EVENT_ALL and
             * toggle on VALUE_CHANGED, exactly like the stock VAS alarm switch
             * (sub_C661410); rows register on the row for LV_EVENT_CLICKED. */
            event_code = kind == CANOPUS_TARGET_ROW_SWITCH ?
                CANOPUS_TARGET_EVENT_ALL : CANOPUS_TARGET_EVENT_CLICKED;
            event_cookie = ((uintptr_t)backend->page_index << 8) |
                           (uintptr_t)(uint32_t)slot;
            add_event(event_object, target_row_event, event_code,
                      (void *)event_cookie);
            backend->row_count++;
        }
        {
            uint8_t selected = kind == CANOPUS_TARGET_ROW_SWITCH ?
                ((node->flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u ? 1u : 0u) :
                1u;
            (void)update_row(object, NULL, primary, secondary, 0, selected);
        }
        set_hidden(object, 0u);
        if (previous == NULL) {
            align_to(object, backend->content_root,
                     CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
            first = object;
        } else {
            align_to(object, previous, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0,
                     gap);
        }
        previous = object;
        backend->bindings[slot].generation = snapshot->generation;
        backend->bindings[slot].key = node->key;
        backend->bindings[slot].event_id = node->event_id;
        used_mask |= UINT32_C(1) << (uint32_t)slot;
    }
    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] != NULL &&
            (used_mask & (UINT32_C(1) << i)) == 0u) {
            set_hidden(backend->rows[i], 1u);
            canopus_memset(&backend->bindings[i], 0,
                           sizeof(backend->bindings[i]));
        }
    }
    for (i = (uint16_t)label_used; i < CANOPUS_TARGET_UI_MAX_LABELS; i++) {
        if (backend->labels[i] != NULL) {
            set_hidden(backend->labels[i], 1u);
        }
    }
    backend->rendered_generation = snapshot->generation;
    canopus_native_probe_record.list_row = (uintptr_t)first;
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
    const struct firmware_page_descriptor *page)
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
    return ((uint32_t)CANOPUS_PROBE_APP_ID << 16) | page_index;
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
    typedef int32_t (*page_finish_fn)(void *);
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
        (void)page_finish(&manager_pages_desc[source_page]);
        return CANOPUS_UI_OK;
    }
    (void)page_goto(target_page_key(target_page), 0u, 0u, 0u);
    /* A push pauses the source. Rendering the destination snapshot into that
     * paused root caused the old two-tap/mixed-content defect. */
    return CANOPUS_UI_OK;
}

static int manager_page_on_create(struct firmware_page_descriptor *page,
                                  void *root, void *start_data)
{
    struct canopus_target_page_context *context;
    int32_t rc;

    (void)start_data;
    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    canopus_native_probe_record.create_count += 1u;
    canopus_native_probe_record.root_object = (uintptr_t)root;
    canopus_memset(&context->backend, 0, sizeof(context->backend));
    context->backend.root = root;
    context->backend.firmware_page = page;
    context->backend.page_index = (uint8_t)page->page_id;
    if (!manager_session_ready) {
        rc = canopus_client_init(&manager_client, &target_device_io, NULL);
        if (rc != CANOPUS_CLIENT_OK ||
            canopus_client_open(&manager_client) != CANOPUS_CLIENT_OK) {
            return -1;
        }
        canopus_manager_init(&manager_model, canopus_client_transport,
                             &manager_client);
        canopus_manager_set_identity(
            &manager_model, "xiaomi-band-10-pro-3.101.030", "3.101.030",
            "CONBINE_LTALM078_T3.101.030_06011854", 1u);
        if (target_refresh_model() != 0) {
            (void)canopus_client_close(&manager_client);
            manager_client.fd = -1;
            return -1;
        }
        manager_session_ready = 1u;
    }
    if (target_select_page_view(context) != 0) {
        return -1;
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

static int manager_page_on_resume(struct firmware_page_descriptor *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL || !context->active) {
        return -1;
    }
    canopus_native_probe_record.resume_count += 1u;
    context->interactive = 1u;
    if (manager_session_ready && target_refresh_model() == 0 &&
        target_select_page_view(context) == 0) {
        (void)canopus_manager_native_render(&context->native);
    }
    return 0;
}

static int manager_page_on_pause(struct firmware_page_descriptor *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    context->interactive = 0u;
    canopus_native_probe_record.pause_count += 1u;
    return 0;
}

static int manager_page_on_destroy(struct firmware_page_descriptor *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    canopus_native_probe_record.destroy_count += 1u;
    canopus_native_probe_record.list_row = 0u;
    canopus_native_probe_record.root_object = 0u;
    if (context->active) {
        target_dialog_dismiss(&context->backend);
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

static const struct firmware_app_descriptor manager_app = {
    .package_name = package_name,
    .launcher_icon_resource = launcher_icon,
    .app_id = CANOPUS_PROBE_APP_ID,
    .launcher_metadata_callback = manager_display_name,
};

static const struct firmware_notification_message loaded_notification = {
    .message_id = UINT64_C(0x43414E4F50555301),
    .title = notification_title,
    .source = notification_title,
    .body = notification_body,
    .small_icon_path = notification_icon,
    .large_icon_path = notification_icon,
    .start_reminder = 1u,
};

static const struct firmware_notification_message module_notification = {
    .message_id = UINT64_C(0x43414E4F50555302),
    .title = notification_title,
    .source = notification_title,
    .body = module_notification_body,
    .small_icon_path = notification_icon,
    .large_icon_path = notification_icon,
    .start_reminder = 1u,
};

int canopus_manager_native_notify_module_installed(void)
{
    typedef int (*notification_insert_fn)(
        const struct firmware_notification_message *);
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;

    if (identity_guard() != 0) {
        return -1;
    }
    return notification_insert(&module_notification);
}

static void __attribute__((constructor, used)) canopus_manager_probe_init(void)
{
    canopus_native_probe_record.magic = CANOPUS_PROBE_MAGIC;
    canopus_native_probe_record.identity_result = identity_guard();
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
    manager_pages_desc[page_index].app_id = CANOPUS_PROBE_APP_ID;
    manager_pages_desc[page_index].on_signal = manager_on_signal;
    manager_pages_desc[page_index].on_create = manager_page_on_create;
    manager_pages_desc[page_index].on_resume = manager_page_on_resume;
    manager_pages_desc[page_index].on_pause = manager_page_on_pause;
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
}

int canopus_manager_native_install(void)
{
    typedef int (*app_install_fn)(const struct firmware_app_descriptor *,
                                  struct firmware_page_descriptor *const *,
                                  uint32_t);
    typedef void *(*app_lookup_fn)(uint16_t);
    typedef int (*launcher_add_fn)(uint16_t);
    typedef int (*notification_insert_fn)(
        const struct firmware_notification_message *);
    struct firmware_page_descriptor *pages[CANOPUS_TARGET_PAGE_COUNT];
    app_install_fn app_install = (app_install_fn)(uintptr_t)FW_APP_INSTALL;
    app_lookup_fn app_lookup = (app_lookup_fn)(uintptr_t)FW_APP_LOOKUP;
    launcher_add_fn launcher_add =
        (launcher_add_fn)(uintptr_t)FW_LAUNCHER_ADD;
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;
    void *installed_app;

    if (canopus_native_probe_record.identity_result != 0) {
        return canopus_native_probe_record.identity_result;
    }
    installed_app = app_lookup(CANOPUS_PROBE_APP_ID);
    if (installed_app != NULL) {
        const char *installed_package =
            *(const char **)((uint8_t *)installed_app + 8u);
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
    canopus_native_probe_record.app_install_result =
        app_install(&manager_app, pages, CANOPUS_TARGET_PAGE_COUNT);
    if (app_lookup(CANOPUS_PROBE_APP_ID) == NULL) {
        canopus_native_probe_record.app_install_result = -100;
        return -100;
    }
    canopus_native_probe_record.launcher_add_result =
        launcher_add(CANOPUS_PROBE_APP_ID);
    canopus_native_probe_record.notification_result =
        notification_insert(&loaded_notification);
    return 0;
}
