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

/* The REDMI Watch 6 keeps the v8-family records (116-byte page with on_destroy
 * at +0x5c and on_ui_destroy at +0x60, 64-byte app descriptor, 88-byte
 * notification) while its UI layer is the MIwear v9 lvx set. p65 section of
 * page_finish 0x1c1325b8: page[23] (on_destroy) and page[24] (on_ui_destroy). */
#if defined(CANOPUS_MGR_REDMI_WATCH)
#define CANOPUS_MGR_V8_RECORDS 1
#endif
/* p65 registers its launcher entry through the native-binding macro. */
#if defined(CANOPUS_MGR_BAND11)
#define CANOPUS_MGR_LAUNCHER_ADD FW_APP_LAUNCHER_ADD
#else
#define CANOPUS_MGR_LAUNCHER_ADD FW_LAUNCHER_ADD
#endif
#include "canopus_ui_host.h"
#include "canopus_memory.h"
#include "canopus_supervisor.h"
#include "canopus_supervisor_platform.h"

#define CANOPUS_TARGET_MANAGER_PAGES 3u /* pages with a Manager model */
#define CANOPUS_TARGET_PAGE_COUNT 4u
#define CANOPUS_TARGET_PAGE_OVERVIEW 0u
#define CANOPUS_TARGET_PAGE_MODULES 1u
#define CANOPUS_TARGET_PAGE_DETAIL 2u
#define CANOPUS_TARGET_PAGE_MODULE_UI 3u
struct canopus_target_page;

typedef int (*page_signal_fn)(struct canopus_target_page *, uint32_t,
                              void *);
typedef int (*page_create_fn)(struct canopus_target_page *, void *,
                              void *);
typedef int (*page_lifecycle_fn)(struct canopus_target_page *);
typedef int (*page_foreground_data_fn)(struct canopus_target_page *,
                                       void *);
typedef int (*page_system_back_fn)(struct canopus_target_page *,
                                   const uint8_t *);

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
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    /* Slot order below follows the recovered type record
     * (targets/<id>/types/canopus_target_page.json) and the exact
     * 3.101.043 IDB: on_destroy_wrapped dispatches descriptor word 23
     * (+0x5c) and on_resume_wrapped dispatches word 22 (+0x58). */
    page_lifecycle_fn on_pause;               /* +0x58 */
    page_lifecycle_fn on_destroy;             /* +0x5c */
    page_lifecycle_fn on_ui_destroy;          /* +0x60 */
    void *extension_callback_100;             /* +0x64 */
    page_system_back_fn extension_callback_104; /* +0x68 */
    void *extension_callback_108;             /* +0x6c */
    void *get_scroll_obj;                     /* +0x70, not in the type record */
#else /* CANOPUS_MGR_BAND11 without V8 records */
    void *on_intermediate;                  /* +0x58 */
    page_lifecycle_fn on_pause;              /* +0x5c, 0xc693a0e */
    page_lifecycle_fn on_stop;               /* +0x60, 0xc693a5c */
    page_lifecycle_fn on_ui_destroy;            /* +0x64, 0xc696aa4 (UI destruction) */
    page_system_back_fn extension_callback_104; /* +0x68 */
    void *extension_callback_108;            /* +0x6c */
    void *extension_callback_112;            /* +0x70 */
    void *extension_callback_116;            /* +0x74 */
#endif /* CANOPUS_MGR_BAND11 */
};

struct firmware_app_descriptor {
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
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
#else /* CANOPUS_MGR_BAND11 without V8 records */
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

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
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
struct canopus_target_page_context {
    struct canopus_ui_host_view view; /* must stay first: hooks cast to the context */
    struct canopus_manager_native_v1 native;
    uint8_t page_index;
    uint8_t counted; /* included in manager_active_pages */
};

/* The one page modules open through ctx->ui (ADR-CAN-010). Its UI context is
 * allocated from the firmware heap while the page exists. */
struct canopus_target_module_page {
    struct canopus_ui_host_view view;
    struct canopus_ui_context_v1 ui;
    struct canopus_target_page *page; /* the Manager's module page or a launcher entry's */
    uint32_t slot;
};

/* A launcher entry a module published through ctx->launcher (ADR-CAN-011): its
 * own firmware app with one page that opens the module's ui page. The firmware
 * holds these descriptors from the moment of app_install, so they live here for
 * the whole boot; there is no way to take one back. */
#define CANOPUS_TARGET_LAUNCHER_FIRST_APP_ID UINT16_C(0x00CC)
enum canopus_target_launcher_state {
    CANOPUS_TARGET_LAUNCHER_FREE = 0,
    CANOPUS_TARGET_LAUNCHER_QUEUED,
    CANOPUS_TARGET_LAUNCHER_INSTALLED,
    CANOPUS_TARGET_LAUNCHER_ADDED,
    CANOPUS_TARGET_LAUNCHER_FAILED,
};
struct canopus_target_launcher_entry {
    struct firmware_app_descriptor app;
    struct canopus_target_page page;
    char package[24];
    char label[CANOPUS_LAUNCHER_LABEL_MAX];
    char icon[CANOPUS_LAUNCHER_ICON_MAX];
    uint32_t slot;
    uint8_t state;
};
static struct canopus_target_launcher_entry launcher_entries[CANOPUS_LAUNCHER_ENTRIES];

static struct canopus_manager_model_v1 manager_model;
static struct canopus_client_v1 manager_client;
static struct canopus_target_page_context
    manager_pages[CANOPUS_TARGET_MANAGER_PAGES];
static struct canopus_target_module_page *module_page; /* heap, while the page exists */
static struct canopus_target_page
    manager_pages_desc[CANOPUS_TARGET_PAGE_COUNT];
static uint32_t manager_active_pages;
static uint32_t manager_pending_detail;
static uint32_t manager_pending_ui_slot; /* module slot + 1 */
static uint8_t manager_session_ready;

_Static_assert(CANOPUS_MANAGER_TARGET_APP_ID <= UINT16_C(0x00FF),
               "system launch animation requires an 8-bit app id");
_Static_assert(CANOPUS_TARGET_LAUNCHER_FIRST_APP_ID > CANOPUS_MANAGER_TARGET_APP_ID + 1u &&
               CANOPUS_TARGET_LAUNCHER_FIRST_APP_ID + CANOPUS_LAUNCHER_ENTRIES - 1u <=
                   UINT16_C(0x00FF),
               "launcher entries need 8-bit app ids clear of the Manager and the demo app");
_Static_assert(CANOPUS_LAUNCHER_ENTRIES == 8u, "one label callback per launcher entry");
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(sizeof(struct canopus_target_page) == 116,
#else /* CANOPUS_MGR_BAND11 without V8 records */
_Static_assert(sizeof(struct canopus_target_page) == 120,
#endif /* CANOPUS_MGR_BAND11 */
               "firmware page descriptor size");
_Static_assert(offsetof(struct canopus_target_page, page_name) == 16,
               "page name offset");
_Static_assert(offsetof(struct canopus_target_page, root_object) == 48,
               "page root offset");
_Static_assert(offsetof(struct canopus_target_page, on_create) == 76,
               "page create offset");
_Static_assert(offsetof(struct canopus_target_page, extension_callback_104) == 104,
               "page system back offset");
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(offsetof(struct canopus_target_page, on_destroy) == 92,
#else /* CANOPUS_MGR_BAND11 without V8 records */
_Static_assert(offsetof(struct canopus_target_page, on_ui_destroy) == 100,
#endif /* CANOPUS_MGR_BAND11 */
               "page destroy offset");
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(sizeof(struct firmware_app_descriptor) == 64,
#else /* CANOPUS_MGR_BAND11 without V8 records */
_Static_assert(sizeof(struct firmware_app_descriptor) == 80,
#endif /* CANOPUS_MGR_BAND11 */
               "firmware app descriptor size");
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(sizeof(struct firmware_notification_message) == 88,
               "firmware notification message size");
#else /* CANOPUS_MGR_BAND11 without V8 records */
_Static_assert(sizeof(struct firmware_notification_message) == 96,
               "firmware notification size");
#endif /* CANOPUS_MGR_BAND11 */
_Static_assert(offsetof(struct firmware_notification_message, title) == 12,
               "notification title offset");
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(offsetof(struct firmware_notification_message, small_icon_path) == 28,
               "notification icon offset");
_Static_assert(offsetof(struct firmware_notification_message, start_reminder) == 80,
#else /* CANOPUS_MGR_BAND11 without V8 records */
_Static_assert(offsetof(struct firmware_notification_message, timestamp) == 48,
               "notification timestamp offset");
_Static_assert(offsetof(struct firmware_notification_message, start_reminder) == 88,
#endif /* CANOPUS_MGR_BAND11 */
               "notification reminder offset");
#if defined(CANOPUS_MGR_BAND11) && !defined(CANOPUS_MGR_V8_RECORDS)
_Static_assert(offsetof(struct firmware_notification_message, callback_data) == 92,
               "notification callback data offset");
#endif /* CANOPUS_MGR_BAND11 */

static const char package_name[] = "com.canopus.manager";
static const char page_name_overview[] = "main";
static const char page_name_modules[] = "modules";
static const char page_name_detail[] = "module_detail";
static const char page_name_module_ui[] = "module_ui";
static const char display_name[] = "Canopus 管理器";
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
static const char notification_title[] = "Canopus";
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

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    if (out == NULL) {
        return;
    }
#else /* CANOPUS_MGR_BAND11 without V8 records */
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
    if (*right != '\0') {
        return 1;
    }
    /* Firmware property blobs end a token with LF, not NUL. */
    return *left != '\0' && *left != '\n' && *left != '\r';
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
    manager_model.safe_mode_reason = device.safe_mode_reason;
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
        module.slot = slot;
        {
            struct canopus_ui_entry_v1 entry;
            if (canopus_supervisor_ui_entry(canopus_supervisor_get(), slot, &entry) == 0) {
                module.has_ui = 1u;
                canopus_memcpy(module.ui_label, entry.label, sizeof(module.ui_label));
            }
        }
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

static struct canopus_target_page_context *context_for_native(
    struct canopus_manager_native_v1 *native)
{
    uint32_t i;

    if (native == NULL) {
        return NULL;
    }
    for (i = 0; i < CANOPUS_TARGET_MANAGER_PAGES; i++) {
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
    for (i = 0; i < CANOPUS_TARGET_MANAGER_PAGES; i++) {
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
    switch (context->page_index) {
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
    case CANOPUS_MANAGER_ROUTE_MODULE_UI:
        target_page = CANOPUS_TARGET_PAGE_MODULE_UI;
        if (manager_model.selected >= manager_model.module_count) {
            return CANOPUS_UI_ERR_STATE;
        }
        manager_pending_ui_slot = manager_model.modules[manager_model.selected].slot + 1u;
        break;
    default:
        return CANOPUS_UI_ERR_ARGUMENT;
    }
    source = context_for_native(native);
    if (source == NULL || !source->view.active || !source->view.interactive) {
        return CANOPUS_UI_ERR_STATE;
    }
    source_page = (uint32_t)source->page_index;
    if (target_page == source_page) {
        return canopus_manager_native_render(native);
    }
    canopus_ui_host_set_interactive(&source->view, 0);
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

/* The back affordance of a non-root Manager page: finish it so the paused page
 * below resumes. */
static void manager_view_back(struct canopus_ui_host_view *view)
{
#ifndef CANOPUS_MGR_BAND11
    typedef int32_t (*page_finish_fn)(void *);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*page_finish_fn)(uint32_t);
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_page_context *context =
        (struct canopus_target_page_context *)view;

    if (context->page_index == CANOPUS_TARGET_PAGE_OVERVIEW) {
        return;
    }
    canopus_ui_host_set_interactive(view, 0);
#ifndef CANOPUS_MGR_BAND11
    (void)((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
        &manager_pages_desc[context->page_index]);
#else /* CANOPUS_MGR_BAND11 */
    ((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
        target_page_key(context->page_index));
#endif /* CANOPUS_MGR_BAND11 */
}

static void manager_view_before_event(
    struct canopus_ui_host_view *view, uint32_t row,
    const struct canopus_ui_host_binding *binding)
{
    (void)view;
    canopus_manager_target_record.click_count += 1u;
    canopus_manager_target_record.clicked_row = row;
    canopus_manager_target_record.clicked_generation = binding->generation;
    canopus_manager_target_record.clicked_key = binding->key;
    canopus_manager_target_record.clicked_event = binding->event_id;
    canopus_manager_target_record.selected_before = manager_model.selected;
}

static void manager_view_after_event(
    struct canopus_ui_host_view *view, uint32_t row,
    const struct canopus_ui_host_binding *binding, int32_t result)
{
    (void)view;
    (void)row;
    (void)binding;
    (void)result;
    canopus_manager_target_record.selected_after = manager_model.selected;
}

static void manager_view_rendered(struct canopus_ui_host_view *view, void *first)
{
    (void)view;
    canopus_manager_target_record.list_row = (uintptr_t)first;
}

static uint32_t target_ui_time_ms(struct canopus_ui_host_view *view)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    (void)view;
    return sup != NULL && sup->platform != NULL && sup->platform->monotonic_ms != NULL
        ? sup->platform->monotonic_ms(sup->platform_cookie) : 0u;
}

static const struct canopus_ui_host_hooks manager_view_hooks = {
    manager_view_back,
    manager_view_before_event,
    manager_view_after_event,
    manager_view_rendered,
    target_ui_time_ms,
};

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
    (void)canopus_supervisor_ui_pump_ensure(canopus_supervisor_get());
    canopus_ui_host_attach(&context->view, root, page, &manager_view_hooks,
                           (uint32_t)page->page_id == CANOPUS_TARGET_PAGE_OVERVIEW
                               ? CANOPUS_UI_HOST_ROOT_PAGE : 0u);
    context->page_index = (uint8_t)page->page_id;
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
    if (context->page_index == CANOPUS_TARGET_PAGE_DETAIL &&
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
    if (context->page_index == CANOPUS_TARGET_PAGE_DETAIL) {
        manager_pending_detail = 0u;
    }
    rc = canopus_manager_native_init(&context->native, &manager_model,
                                     &canopus_ui_host_backend, &context->view);
    if (rc != CANOPUS_UI_OK) {
        canopus_ui_host_detach(&context->view);
        return -1;
    }
    canopus_ui_host_bind(&context->view, &context->native.ui);
    canopus_manager_native_set_router(&context->native, target_route, NULL);
    canopus_manager_native_set_refresh(&context->native, target_refresh,
                                       context);
    if (!context->counted) {
        context->counted = 1u;
        manager_active_pages++;
    }
    canopus_ui_host_set_interactive(&context->view, 1);
    return 0;
}

static int manager_page_on_resume(struct canopus_target_page *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL || !context->view.active) {
        return -1;
    }
    canopus_manager_target_record.resume_count += 1u;
    canopus_ui_host_set_interactive(&context->view, 1);
    if (manager_session_ready && target_refresh_model() == 0 &&
        target_select_page_view(context) == 0) {
        (void)canopus_manager_native_render(&context->native);
    }
    return 0;
}

void canopus_manager_native_refresh(void)
{
    uint32_t i;
    if (!manager_session_ready) return;
    for (i = 0u; i < CANOPUS_TARGET_MANAGER_PAGES; i++) {
        struct canopus_target_page_context *context = &manager_pages[i];
        if (!context->view.active || !context->view.interactive) continue;
        if (target_refresh_model() == 0 && target_select_page_view(context) == 0)
            (void)canopus_manager_native_render(&context->native);
        return;
    }
}

static int manager_page_on_pause(struct canopus_target_page *page)
{
    struct canopus_target_page_context *context;

    context = context_for_page(page);
    if (context == NULL) {
        return -1;
    }
    canopus_ui_host_set_interactive(&context->view, 0);
    canopus_manager_target_record.pause_count += 1u;
    return 0;
}

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
static int manager_page_on_destroy(struct canopus_target_page *page)
#else /* CANOPUS_MGR_BAND11 without V8 records */
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
    if (context->counted) {
        context->counted = 0u;
        if (manager_active_pages > 0u) {
            manager_active_pages--;
        }
    }
    canopus_ui_host_detach(&context->view);
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

/* ---- the page modules open through ctx->ui (ADR-CAN-010) ------------- */

static void module_page_finish(void)
{
#ifndef CANOPUS_MGR_BAND11
    typedef int32_t (*page_finish_fn)(void *);
    canopus_ui_host_set_interactive(&module_page->view, 0);
    (void)((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(module_page->page);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*page_finish_fn)(uint32_t);
    canopus_ui_host_set_interactive(&module_page->view, 0);
    ((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
        ((uint32_t)module_page->page->app_id << 16) | module_page->page->page_id);
#endif /* CANOPUS_MGR_BAND11 */
}

/* The module's registered entry while its page is shown; nonzero once the
 * module was unloaded or replaced the entry (its callbacks are then not called). */
static int module_page_entry(struct canopus_ui_entry_v1 *entry,
                             const struct canopus_context_v1 **ctx)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();

    if (module_page == NULL || !module_page->view.active ||
        canopus_supervisor_ui_entry(sup, module_page->slot, entry) != 0) {
        return -1;
    }
    *ctx = canopus_supervisor_context(sup, module_page->slot);
    return *ctx != NULL ? 0 : -1;
}

static int32_t module_page_event(void *cookie,
                                 const struct canopus_ui_event_v1 *event)
{
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    struct canopus_target_module_page *page = module_page;
    int32_t rc;

    (void)cookie;
    if (module_page_entry(&entry, &ctx) != 0 || entry.on_event == NULL) {
        return CANOPUS_UI_ERR_STATE;
    }
    rc = entry.on_event(ctx, &page->ui, event, entry.user_data);
    if (module_page == page) { /* the event may have closed its own page */
        canopus_ui_context_abort_pending(&page->ui);
    }
    return rc;
}

/* Title and completed system back share the module's route-pop contract. */
static void module_page_back(struct canopus_ui_host_view *view)
{
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    struct canopus_target_module_page *page = module_page;

    if (page == NULL || view != &page->view || !view->active || !view->interactive) {
        return;
    }
    if (module_page_entry(&entry, &ctx) == 0 && entry.on_back != NULL) {
        int32_t handled = entry.on_back(ctx, &page->ui, entry.user_data);
        /* close_page may finish/destroy the page synchronously, or leave it
         * awaiting destruction with interaction disabled. Do not finish twice. */
        if (module_page != page) return;
        canopus_ui_context_abort_pending(&page->ui);
        if (handled == 0 || !page->view.active || !page->view.interactive) {
            return;
        }
    }
    module_page_finish();
}

static uint16_t module_page_back_completed_phase(void)
{
    if (strings_differ(CANOPUS_TARGET_ID, "xiaomi-band-11-4.100.155") == 0 ||
        strings_differ(CANOPUS_TARGET_ID, "xiaomi-band-11-4.100.179") == 0) {
        return UINT16_C(0x48);
    }
    if (strings_differ(CANOPUS_TARGET_ID, "xiaomi-band-10-pro-3.101.043") == 0) {
        return UINT16_C(0x4a);
    }
    return 0u;
}

/* Exact-target static ABI: descriptor +104 receives the matched descriptor
 * and borrowed event+4 payload; payload+8 is a little-endian uint16 phase.
 * Nonzero consumes the gesture, including after a synchronous page finish.
 * See docs/investigations/native-system-back.md; no capability promotion. */
static int module_page_system_back(struct canopus_target_page *descriptor,
                                   const uint8_t *payload)
{
    struct canopus_target_module_page *page = module_page;
    uint16_t phase, completed = module_page_back_completed_phase();

    if (page == NULL || page->page != descriptor || !page->view.active ||
        !page->view.interactive || payload == NULL || completed == 0u) {
        return 0;
    }
    phase = (uint16_t)((uint16_t)payload[8] | ((uint16_t)payload[9] << 8));
    if (phase != completed) return 0;
    module_page_back(&page->view);
    return 1;
}

static const struct canopus_ui_host_hooks module_view_hooks = {
    module_page_back,
    NULL,
    NULL,
    NULL,
    target_ui_time_ms,
};

static void module_page_release(void)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_target_module_page *page = module_page;

    module_page = NULL;
    if (page != NULL) {
        canopus_ui_host_detach(&page->view);
        sup->platform->heap_release(sup->platform_cookie, page);
    }
}

static struct canopus_target_launcher_entry *launcher_entry_of(
    const struct canopus_target_page *page);

/* The module whose page `page` opens: the one picked in the Manager, or the
 * owner of the launcher entry that was chosen. */
static int module_page_slot(const struct canopus_target_page *page, uint32_t *slot)
{
    const struct canopus_target_launcher_entry *launcher = launcher_entry_of(page);

    if (launcher != NULL) {
        *slot = launcher->slot;
        return 0;
    }
    if (manager_pending_ui_slot == 0u) {
        return -1;
    }
    *slot = manager_pending_ui_slot - 1u;
    manager_pending_ui_slot = 0u;
    return 0;
}

static int module_page_on_create(struct canopus_target_page *page, void *root,
                                 void *start_data)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    uint32_t slot;

    (void)start_data;
    if (module_page != NULL || module_page_slot(page, &slot) != 0) {
        return -1;
    }
    if (canopus_supervisor_ui_entry(sup, slot, &entry) != 0 ||
        sup->platform == NULL || sup->platform->heap_alloc == NULL ||
        sup->platform->heap_release == NULL) {
        return -1;
    }
    module_page = (struct canopus_target_module_page *)sup->platform->heap_alloc(
        sup->platform_cookie, sizeof(*module_page));
    if (module_page == NULL) {
        return -1;
    }
    canopus_memset(module_page, 0, sizeof(*module_page));
    (void)canopus_supervisor_ui_pump_ensure(sup);
    canopus_ui_host_attach(&module_page->view, root, page, &module_view_hooks, 0u);
    module_page->page = page;
    module_page->slot = slot;
    if (canopus_ui_context_init(&module_page->ui, &canopus_ui_host_backend,
                                &module_page->view, module_page_event, NULL) !=
        CANOPUS_UI_OK) {
        module_page_release();
        return -1;
    }
    canopus_ui_host_bind(&module_page->view, &module_page->ui);
    canopus_ui_host_set_interactive(&module_page->view, 1);
    ctx = canopus_supervisor_context(sup, slot);
    if (ctx == NULL || entry.on_open(ctx, &module_page->ui, entry.user_data) != 0) {
        module_page_release();
        return -1;
    }
    if (module_page == NULL) {
        return -1;
    }
    canopus_ui_context_abort_pending(&module_page->ui);
    if (canopus_ui_current(&module_page->ui) == NULL) {
        module_page_release();
        return -1;
    }
    return 0;
}

static int module_page_on_resume(struct canopus_target_page *page)
{
    (void)page;
    if (module_page != NULL) {
        canopus_ui_host_set_interactive(&module_page->view, 1);
    }
    return 0;
}

static int module_page_on_pause(struct canopus_target_page *page)
{
    (void)page;
    if (module_page != NULL) {
        canopus_ui_host_set_interactive(&module_page->view, 0);
    }
    return 0;
}

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
static int module_page_on_destroy(struct canopus_target_page *page)
#else /* CANOPUS_MGR_BAND11 without V8 records */
static int module_page_on_ui_destroy(struct canopus_target_page *page)
#endif /* CANOPUS_MGR_BAND11 */
{
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;

    (void)page;
    if (module_page_entry(&entry, &ctx) == 0 && entry.on_close != NULL) {
        entry.on_close(ctx, entry.user_data);
    }
    module_page_release();
    return 0;
}

/* The module closed its page or is gone: finish the page if it is its own. */
int canopus_manager_target_module_page_close(uint32_t slot)
{
    if (module_page == NULL || !module_page->view.active ||
        module_page->slot != slot) {
        return 0;
    }
    module_page_finish();
    return 1;
}

/* ---- module launcher entries (ADR-CAN-011) --------------------------- */

#define LAUNCHER_LABEL_FN(n) \
    static const char *launcher_label_##n(void) { return launcher_entries[n].label; }
LAUNCHER_LABEL_FN(0) LAUNCHER_LABEL_FN(1) LAUNCHER_LABEL_FN(2) LAUNCHER_LABEL_FN(3)
LAUNCHER_LABEL_FN(4) LAUNCHER_LABEL_FN(5) LAUNCHER_LABEL_FN(6) LAUNCHER_LABEL_FN(7)
static const char *(*const launcher_label_fns[CANOPUS_LAUNCHER_ENTRIES])(void) = {
    launcher_label_0, launcher_label_1, launcher_label_2, launcher_label_3,
    launcher_label_4, launcher_label_5, launcher_label_6, launcher_label_7,
};

static struct canopus_target_launcher_entry *launcher_entry_of(
    const struct canopus_target_page *page)
{
    uint32_t i;

    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        if (page == &launcher_entries[i].page) {
            return &launcher_entries[i];
        }
    }
    return NULL;
}

static void launcher_copy(char *to, uint32_t size, const char *from)
{
    uint32_t i;

    for (i = 0u; i + 1u < size && from != NULL && from[i] != '\0'; i++) {
        to[i] = from[i];
    }
    to[i] = '\0';
}

int canopus_manager_target_launcher_publish(uint32_t slot, const char *label,
                                            const char *icon)
{
    struct canopus_target_launcher_entry *entry = NULL;
    uint32_t i;

    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        if (launcher_entries[i].state == CANOPUS_TARGET_LAUNCHER_FREE) {
            entry = &launcher_entries[i];
            break;
        }
    }
    if (entry == NULL) {
        return -12;
    }
    launcher_copy(entry->label, sizeof(entry->label), label);
    launcher_copy(entry->icon, sizeof(entry->icon), icon);
    launcher_copy(entry->package, sizeof(entry->package), "com.canopus.module.");
    entry->package[19] = (char)('0' + i);
    entry->package[20] = '\0';
    entry->slot = slot;
    entry->state = CANOPUS_TARGET_LAUNCHER_QUEUED;
    return 0;
}

/* Fills the app and page records the firmware is handed at app_install. */
static void launcher_entry_init(struct canopus_target_launcher_entry *entry,
                                uint32_t index)
{
    uint16_t app_id = (uint16_t)(CANOPUS_TARGET_LAUNCHER_FIRST_APP_ID + index);

    canopus_memset(&entry->app, 0, sizeof(entry->app));
    entry->app.package_name = entry->package;
    entry->app.launcher_icon_resource = entry->icon[0] != '\0' ? entry->icon : launcher_icon;
    entry->app.app_id = app_id;
    entry->app.launcher_metadata_callback = launcher_label_fns[index];
    canopus_memset(&entry->page, 0, sizeof(entry->page));
    entry->page.page_name = page_name_module_ui;
    entry->page.page_id = 0u;
    entry->page.app_id = app_id;
    entry->page.on_signal = manager_on_signal;
    entry->page.on_create = module_page_on_create;
    entry->page.on_resume = module_page_on_resume;
    entry->page.on_pause = module_page_on_pause;
    if (module_page_back_completed_phase() != 0u) {
        entry->page.extension_callback_104 = module_page_system_back;
    }
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    entry->page.on_destroy = module_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 without V8 records */
    entry->page.on_ui_destroy = module_page_on_ui_destroy;
#endif /* CANOPUS_MGR_BAND11 */
}

int canopus_manager_target_launcher_step(void)
{
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    typedef int (*launcher_add_fn)(uint16_t);
#else /* CANOPUS_MGR_BAND11 without V8 records */
    typedef void (*launcher_add_fn)(uint16_t);
#endif /* CANOPUS_MGR_BAND11 */
    typedef int (*launcher_install_fn)(struct firmware_app_descriptor *,
                                       struct canopus_target_page *const *, uint32_t);
    typedef void *(*launcher_lookup_fn)(uint16_t);
    launcher_install_fn app_install = (launcher_install_fn)(uintptr_t)FW_APP_INSTALL;
    launcher_lookup_fn app_lookup = (launcher_lookup_fn)(uintptr_t)FW_APP_LOOKUP;
#ifndef CANOPUS_MGR_BAND11
    launcher_add_fn launcher_add = (launcher_add_fn)(uintptr_t)CANOPUS_MGR_LAUNCHER_ADD;
#else /* CANOPUS_MGR_BAND11 */
    launcher_add_fn launcher_add = (launcher_add_fn)(uintptr_t)CANOPUS_MGR_LAUNCHER_ADD;
#endif /* CANOPUS_MGR_BAND11 */
    uint32_t i;

    if (canopus_manager_target_record.identity_result != 0) {
        return canopus_manager_target_record.identity_result;
    }
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        struct canopus_target_launcher_entry *entry = &launcher_entries[i];
        struct canopus_target_page *pages[1];

        if (entry->state != CANOPUS_TARGET_LAUNCHER_QUEUED) {
            continue;
        }
        launcher_entry_init(entry, i);
        pages[0] = &entry->page;
        (void)app_install(&entry->app, pages, 1u);
        if (app_lookup(entry->app.app_id) == NULL) {
            entry->state = CANOPUS_TARGET_LAUNCHER_FAILED;
            return -5;
        }
        entry->state = CANOPUS_TARGET_LAUNCHER_INSTALLED;
        return 1;
    }
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        struct canopus_target_launcher_entry *entry = &launcher_entries[i];

        if (entry->state != CANOPUS_TARGET_LAUNCHER_INSTALLED) {
            continue;
        }
        (void)launcher_add(entry->app.app_id);
        entry->state = CANOPUS_TARGET_LAUNCHER_ADDED;
        return 1;
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
static struct band11_notification_context module_notification_context;

#endif /* CANOPUS_MGR_BAND11 */

static const struct firmware_notification_message module_notification = {
    .message_id = UINT64_C(0x43414E4F50555302),
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    .title = notification_title,
    .source = notification_title,
    .body = module_notification_body,
    .small_icon_path = notification_icon,
    .large_icon_path = notification_icon,
#else /* CANOPUS_MGR_BAND11 without V8 records */
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

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
int canopus_manager_native_notify_module_installed(void)
#else /* CANOPUS_MGR_BAND11 without V8 records */
static int target_notify(const struct firmware_notification_message *message)
#endif /* CANOPUS_MGR_BAND11 */
{
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    typedef int (*notification_insert_fn)(
#else /* CANOPUS_MGR_BAND11 without V8 records */
    typedef void (*notification_insert_fn)(
#endif /* CANOPUS_MGR_BAND11 */
        const struct firmware_notification_message *);
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;

    if (identity_guard() != 0) {
        return -1;
    }
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    return notification_insert(&module_notification);
#else /* CANOPUS_MGR_BAND11 without V8 records */
    /* The stock entry returns free() residue, not a delivery result. */
    notification_insert(message);
    return 0;
#endif /* CANOPUS_MGR_BAND11 */
}

#if defined(CANOPUS_MGR_BAND11) && !defined(CANOPUS_MGR_V8_RECORDS)
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
int canopus_manager_native_notify(const char *source, const char *title, const char *body,
                                  const char *icon)
{
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    typedef int (*notification_insert_fn)(
        const struct firmware_notification_message *);
    notification_insert_fn notification_insert =
        (notification_insert_fn)(uintptr_t)FW_NOTIFICATION_INSERT;
    static uint32_t counter;
#endif /* CANOPUS_MGR_BAND11 */
    struct firmware_notification_message message;
#if defined(CANOPUS_MGR_BAND11) && !defined(CANOPUS_MGR_V8_RECORDS)
    uint32_t slot = notify_counter % BAND11_NOTIFY_RING;
#endif /* CANOPUS_MGR_BAND11 */

#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    if (identity_guard() != 0) {
        return -1;
    }
#endif /* CANOPUS_MGR_BAND11 */
    canopus_memset(&message, 0, sizeof(message));
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    message.small_icon_path = (void *)(icon != 0 ? icon : notification_icon);
    message.large_icon_path = (void *)(icon != 0 ? icon : notification_icon);
#else /* CANOPUS_MGR_BAND11 without V8 records */
    message.small_icon_path = (void *)(icon != 0 ? icon : launcher_icon);
    message.large_icon_path = (void *)(icon != 0 ? icon : launcher_icon);
#endif /* CANOPUS_MGR_BAND11 */
    message.start_reminder = 1u;
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    message.message_id = UINT64_C(0x43414E4F50555310) + (counter++ & 0xffu);
#else /* CANOPUS_MGR_BAND11 without V8 records */

    canopus_memset(&notify_contexts[slot], 0, sizeof(notify_contexts[slot]));
    message.message_id = UINT64_C(0x43414E4F50555310) + (notify_counter++ & 0xffu);
#endif /* CANOPUS_MGR_BAND11 */
    message.title = (void *)title;
    message.source = (void *)source;
    message.body = (void *)body;
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    return notification_insert(&message) < 0 ? -1 : 0;
#else /* CANOPUS_MGR_BAND11 without V8 records */
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
    if (page_index == CANOPUS_TARGET_PAGE_MODULE_UI) {
        manager_pages_desc[page_index].on_create = module_page_on_create;
        manager_pages_desc[page_index].on_resume = module_page_on_resume;
        manager_pages_desc[page_index].on_pause = module_page_on_pause;
        if (module_page_back_completed_phase() != 0u) {
            manager_pages_desc[page_index].extension_callback_104 = module_page_system_back;
        }
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
        manager_pages_desc[page_index].on_destroy = module_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 without V8 records */
        manager_pages_desc[page_index].on_destroy = module_page_on_destroy;
#endif /* CANOPUS_MGR_BAND11 */
        return;
    }
    manager_pages_desc[page_index].on_create = manager_page_on_create;
    manager_pages_desc[page_index].on_resume = manager_page_on_resume;
    manager_pages_desc[page_index].on_pause = manager_page_on_pause;
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 without V8 records */
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
#endif /* CANOPUS_MGR_BAND11 */
}

int canopus_manager_native_install(void)
{
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    typedef int (*app_install_fn)(const struct firmware_app_descriptor *,
#else /* CANOPUS_MGR_BAND11 without V8 records */
    typedef int (*app_install_fn)(struct firmware_app_descriptor *,
#endif /* CANOPUS_MGR_BAND11 */
                                  struct canopus_target_page *const *,
                                  uint32_t);
    typedef void *(*app_lookup_fn)(uint16_t);
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    typedef int (*launcher_add_fn)(uint16_t);
#else /* CANOPUS_MGR_BAND11 without V8 records */
    typedef void (*launcher_add_fn)(uint16_t);
#endif /* CANOPUS_MGR_BAND11 */
    struct canopus_target_page *pages[CANOPUS_TARGET_PAGE_COUNT];
    app_install_fn app_install = (app_install_fn)(uintptr_t)FW_APP_INSTALL;
    app_lookup_fn app_lookup = (app_lookup_fn)(uintptr_t)FW_APP_LOOKUP;
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    launcher_add_fn launcher_add =
        (launcher_add_fn)(uintptr_t)CANOPUS_MGR_LAUNCHER_ADD;
#else /* CANOPUS_MGR_BAND11 without V8 records */
    launcher_add_fn launcher_add = (launcher_add_fn)(uintptr_t)CANOPUS_MGR_LAUNCHER_ADD;
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
        if (installed_package == NULL ||
            strings_differ(installed_package, package_name) != 0) return -101;
        canopus_supervisor_boot_ready(canopus_supervisor_get());
        return 0;
    }

    target_descriptor_init(CANOPUS_TARGET_PAGE_OVERVIEW, page_name_overview,
                           0u);
    target_descriptor_init(CANOPUS_TARGET_PAGE_MODULES, page_name_modules, 1u);
    target_descriptor_init(CANOPUS_TARGET_PAGE_DETAIL, page_name_detail, 2u);
    target_descriptor_init(CANOPUS_TARGET_PAGE_MODULE_UI, page_name_module_ui, 3u);
    pages[0] = &manager_pages_desc[0];
    pages[1] = &manager_pages_desc[1];
    pages[2] = &manager_pages_desc[2];
    pages[3] = &manager_pages_desc[3];
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
    canopus_supervisor_boot_ready(canopus_supervisor_get());
    return 0;
}

int canopus_manager_native_open(void)
{
    typedef int32_t (*page_goto_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    page_goto_fn page_goto = (page_goto_fn)(uintptr_t)FW_ACTIVITY_NAVIGATE;

    if (canopus_manager_target_record.identity_result != 0) return -1;
    return page_goto(target_page_key(CANOPUS_TARGET_PAGE_OVERVIEW), 0u, 0u, 0u) < 0
               ? -1 : 0;
}
