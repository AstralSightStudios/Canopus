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

/* One page of a module app (ADR-CAN-010). A single-page entry owns exactly
 * one; a multi-page entry (canopus_ui_entry_v1.page_count above 0) owns one per
 * possible route and pushes/finishes them through the firmware page stack, so
 * every route change is a stock push/pop transition. A UI context is ~14 KB of
 * firmware heap, so only pages that are actually on the stack carry one: a
 * paused page below the top keeps its native tree (already painted, frozen by
 * the firmware for the transition) and gets a fresh context when it resumes. */
struct canopus_target_module_page {
    struct canopus_ui_host_view view;
    struct canopus_ui_context_v1 *ui; /* NULL while not the top page */
    struct canopus_target_page *page; /* the Manager's module page or a launcher entry's */
    void *root;                       /* page root captured at on_create */
    uint32_t slot;
    uint32_t route;
};

/* Slot 0 is the entry page; the rest are multi-page routes. */
#define CANOPUS_TARGET_MODULE_PAGES CANOPUS_UI_ENTRY_PAGES_MAX

/* Managed UI and native pages share the Supervisor's persistent identity and
 * one boot-resident registration queue. */
enum canopus_target_launcher_state {
    CANOPUS_TARGET_LAUNCHER_FREE = 0,
    CANOPUS_TARGET_LAUNCHER_QUEUED,
    CANOPUS_TARGET_LAUNCHER_INSTALLED,
    CANOPUS_TARGET_LAUNCHER_ADDED,
    CANOPUS_TARGET_LAUNCHER_FAILED,
};
struct canopus_target_launcher_entry {
    struct firmware_app_descriptor app;
    /* A managed UI entry owns page_count descriptors: pages[0] is the entry
     * page, the rest are multi-page routes. Native page apps use native_pages
     * instead and leave page_count at 0. */
    struct canopus_target_page pages[CANOPUS_TARGET_MODULE_PAGES];
    struct canopus_target_module_page *module_pages[CANOPUS_TARGET_MODULE_PAGES];
    uint32_t page_count; /* managed UI routes; 0 for a native page app */
    char package[CANOPUS_SUP_NATIVE_PACKAGE_MAX];
    struct canopus_target_page *native_pages[CANOPUS_NATIVE_APP_PAGES_MAX];
    uint32_t native_page_count;
    char label[CANOPUS_LAUNCHER_LABEL_MAX];
    char icon[CANOPUS_LAUNCHER_ICON_MAX];
    uint32_t slot;
    uint8_t state;
};
static struct canopus_target_launcher_entry *launcher_entries[CANOPUS_LAUNCHER_ENTRIES];

static struct canopus_manager_model_v1 manager_model;
static struct canopus_client_v1 manager_client;
static struct canopus_target_page_context
    manager_pages[CANOPUS_TARGET_MANAGER_PAGES];
static struct canopus_target_module_page *module_page; /* heap, while it is top */
/* Single-page entries have no launcher record of their own; the Manager's
 * module page carries this one route instead. */
static struct canopus_target_module_page module_page_scratch;
static struct canopus_target_page
    manager_pages_desc[CANOPUS_TARGET_PAGE_COUNT];
static uint32_t manager_active_pages;
static uint32_t manager_pending_detail;
static uint32_t manager_pending_ui_slot; /* module slot + 1 */
static uint8_t manager_session_ready;

_Static_assert(CANOPUS_MANAGER_TARGET_APP_ID <= UINT16_C(0x00FF),
               "system launch animation requires an 8-bit app id");
_Static_assert(CANOPUS_SUP_NATIVE_APP_FIRST_ID > CANOPUS_MANAGER_TARGET_APP_ID + 1u &&
               CANOPUS_SUP_NATIVE_APP_FIRST_ID + CANOPUS_SUP_NATIVE_APP_IDS - 1u <=
                   UINT16_C(0x00FF),
               "native identities need 8-bit app ids clear of the Manager and legacy demo");
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

/* ---- module pages opened through ctx->ui (ADR-CAN-010) --------------- */

static void module_page_finish(struct canopus_target_module_page *page);
static int32_t module_page_event(void *cookie,
                                 const struct canopus_ui_event_v1 *event);
static void module_page_back(struct canopus_ui_host_view *view);

static const struct canopus_ui_host_hooks module_view_hooks = {
    module_page_back,
    NULL,
    NULL,
    NULL,
    target_ui_time_ms,
};

/* Page descriptor -> the module page that owns it. A multi-page entry owns one
 * descriptor per route; a single-page entry (and every launcher entry) owns
 * exactly one. */
static struct canopus_target_module_page *module_page_of(
    const struct canopus_target_page *descriptor)
{
    uint32_t i, route;

    if (descriptor == NULL) return NULL;
    if (module_page != NULL && module_page->page == descriptor) return module_page;
    if (descriptor == &manager_pages_desc[CANOPUS_TARGET_PAGE_MODULE_UI]) {
        return &module_page_scratch;
    }
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        struct canopus_target_launcher_entry *entry = launcher_entries[i];
        if (entry == NULL) continue;
        for (route = 0u; route < entry->page_count; route++) {
            if (&entry->pages[route] == descriptor) {
                return entry->module_pages[route];
            }
        }
    }
    return NULL;
}

/* Releases the UI context of `page` without finishing the firmware page: used
 * when a route pushes on top of it, and when it is destroyed anyway. */
static void module_page_drop_ui(struct canopus_target_module_page *page)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();

    canopus_ui_host_set_interactive(&page->view, 0);
    if (page->ui != NULL) {
        sup->platform->heap_release(sup->platform_cookie, page->ui);
        page->ui = NULL;
    }
    canopus_ui_host_detach(&page->view);
}

/* Builds the UI context of `page` and renders `route` into it. Used both by
 * on_create and when a paused page resumes as the new top page. */
static int module_page_bind_ui(struct canopus_target_module_page *page, void *root)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    uint32_t slot = page->slot;

    if (canopus_supervisor_ui_entry(sup, slot, &entry) != 0 ||
        sup->platform == NULL || sup->platform->heap_alloc == NULL ||
        sup->platform->heap_release == NULL) {
        return -1;
    }
    page->ui = (struct canopus_ui_context_v1 *)sup->platform->heap_alloc(
        sup->platform_cookie, sizeof(*page->ui));
    if (page->ui == NULL) return -1;
    (void)canopus_supervisor_ui_pump_ensure(sup);
    canopus_ui_host_attach(&page->view, root, page->page, &module_view_hooks, 0u);
    if (canopus_ui_context_init(page->ui, &canopus_ui_host_backend,
                                &page->view, module_page_event, page) !=
        CANOPUS_UI_OK) {
        module_page_drop_ui(page);
        return -1;
    }
    canopus_ui_host_bind(&page->view, page->ui);
    canopus_ui_host_set_interactive(&page->view, 1);
    ctx = canopus_supervisor_context(sup, slot);
    if (ctx == NULL) {
        module_page_drop_ui(page);
        return -1;
    }
    /* on_open always renders route 0. A deeper route re-renders itself from the
     * module's model, which is exactly what on_back does after a pop; the
     * module owns that model, not the host. */
    if (page->route == 0u) {
        if (entry.on_open(ctx, page->ui, entry.user_data) != 0) {
            module_page_drop_ui(page);
            return -1;
        }
    }
    canopus_ui_context_abort_pending(page->ui);
    if (canopus_ui_current(page->ui) == NULL) {
        module_page_drop_ui(page);
        return -1;
    }
    return 0;
}

static void module_page_finish(struct canopus_target_module_page *page)
{
#ifndef CANOPUS_MGR_BAND11
    typedef int32_t (*page_finish_fn)(void *);
    canopus_ui_host_set_interactive(&page->view, 0);
    (void)((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(page->page);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*page_finish_fn)(uint32_t);
    canopus_ui_host_set_interactive(&page->view, 0);
    ((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
        ((uint32_t)page->page->app_id << 16) | page->page->page_id);
#endif /* CANOPUS_MGR_BAND11 */
}

/* The module's registered entry while `page` is shown; nonzero once the module
 * was unloaded or replaced the entry (its callbacks are then not called). */
static int module_page_entry(const struct canopus_target_module_page *page,
                             struct canopus_ui_entry_v1 *entry,
                             const struct canopus_context_v1 **ctx)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();

    if (page == NULL || !page->view.active ||
        canopus_supervisor_ui_entry(sup, page->slot, entry) != 0) {
        return -1;
    }
    *ctx = canopus_supervisor_context(sup, page->slot);
    return *ctx != NULL ? 0 : -1;
}

static int32_t module_page_event(void *cookie,
                                 const struct canopus_ui_event_v1 *event)
{
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    struct canopus_target_module_page *page =
        (struct canopus_target_module_page *)cookie;
    int32_t rc;

    if (page != module_page || page->ui == NULL ||
        module_page_entry(page, &entry, &ctx) != 0 || entry.on_event == NULL) {
        return CANOPUS_UI_ERR_STATE;
    }
    rc = entry.on_event(ctx, page->ui, event, entry.user_data);
    if (module_page == page && page->ui != NULL) { /* the event may have closed it */
        canopus_ui_context_abort_pending(page->ui);
    }
    return rc;
}

/* Runs the module's own back contract for the live top page.
 *
 * Returns 1 when the module consumed the back itself: an internal route pop, a
 * replacement tree, or a close_page that already finished the page. Returns 0
 * when the module declined, which leaves the page's fate to the caller.
 *
 * The two callers differ, and that difference is the whole point. A title-back
 * tap has no firmware default behind it, so it finishes the page. A completed
 * system swipe does: sub_C69BB50 keeps its own page-out transition only while
 * descriptor +104 answers zero (docs/investigations/native-system-back.md).
 * Finishing the page here would answer nonzero and cost the stock animation. */
static int module_page_back_handled(struct canopus_ui_host_view *view)
{
    struct canopus_ui_entry_v1 entry;
    const struct canopus_context_v1 *ctx;
    struct canopus_target_module_page *page = module_page;

    if (page == NULL || view != &page->view || page->ui == NULL ||
        !view->active || !view->interactive) {
        return 1;
    }
    if (module_page_entry(page, &entry, &ctx) == 0 && entry.on_back != NULL) {
        int32_t handled = entry.on_back(ctx, page->ui, entry.user_data);
        /* close_page may finish/destroy the page synchronously, or leave it
         * awaiting destruction with interaction disabled. Do not finish twice. */
        if (module_page != page) return 1;
        canopus_ui_context_abort_pending(page->ui);
        if (handled == 0 || !page->view.active || !page->view.interactive) {
            return 1;
        }
    }
    return 0;
}

/* Title and completed system back share the module's route-pop contract. */
static void module_page_back(struct canopus_ui_host_view *view)
{
    if (!module_page_back_handled(view)) module_page_finish(module_page);
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
 * Nonzero consumes the gesture. See docs/investigations/native-system-back.md;
 * no capability promotion. */
static int module_page_system_back(struct canopus_target_page *descriptor,
                                   const uint8_t *payload)
{
    struct canopus_target_module_page *page = module_page_of(descriptor);
    uint16_t phase, completed = module_page_back_completed_phase();

    if (page == NULL || page != module_page || page->ui == NULL ||
        !page->view.active || !page->view.interactive ||
        payload == NULL || completed == 0u) {
        return 0;
    }
    phase = (uint16_t)((uint16_t)payload[8] | ((uint16_t)payload[9] << 8));
    if (phase != completed) return 0;
    /* Zero lets sub_C69BB50 finish this page itself and play the stock page-out
     * transition, which is the whole reason a declined back is not finished
     * here. Only an internal route pop is consumed. */
    return module_page_back_handled(&page->view);
}

static struct canopus_target_launcher_entry *launcher_entry_of(
    const struct canopus_target_page *page);

/* The module whose page `page` opens: the one picked in the Manager, or the
 * owner of the launcher entry that was chosen. */
static int module_page_slot(const struct canopus_target_page *page, uint32_t *slot)
{
    const struct canopus_target_launcher_entry *launcher = launcher_entry_of(page);

    if (launcher != NULL) {
        struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
        if (launcher->slot >= CANOPUS_SUP_MODULE_SLOTS ||
            strings_differ(launcher->package + 19u,
                           (const char *)sup->modules[launcher->slot].module_id) != 0)
            return -1;
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
    struct canopus_target_launcher_entry *launcher = launcher_entry_of(page);
    struct canopus_target_module_page *slot_page;
    uint32_t slot, route = 0u;

    (void)start_data;
    if (launcher != NULL) {
        slot = launcher->slot;
        route = (uint32_t)(page - launcher->pages);
        if (route >= launcher->page_count) return -1;
    } else if (module_page_slot(page, &slot) != 0) {
        return -1;
    }
    /* The firmware may create a deeper route directly; only one page is top. */
    if (module_page != NULL && module_page->page != page) {
        module_page_drop_ui(module_page);
        module_page = NULL;
    }
    if (launcher != NULL) {
        slot_page = launcher->module_pages[route];
        if (slot_page == NULL) {
            slot_page = (struct canopus_target_module_page *)sup->platform->heap_alloc(
                sup->platform_cookie, sizeof(*slot_page));
            if (slot_page == NULL) return -1;
            launcher->module_pages[route] = slot_page;
        }
    } else {
        slot_page = &module_page_scratch;
    }
    canopus_memset(slot_page, 0, sizeof(*slot_page));
    slot_page->page = page;
    slot_page->slot = slot;
    slot_page->route = route;
    slot_page->root = root;
    module_page = slot_page;
    if (module_page_bind_ui(slot_page, root) != 0) {
        module_page = NULL;
        return -1;
    }
    return 0;
}

static int module_page_on_resume(struct canopus_target_page *page)
{
    struct canopus_target_launcher_entry *launcher = launcher_entry_of(page);
    struct canopus_target_module_page *slot_page = module_page_of(page);

    (void)launcher;
    /* A resume always follows a pop below, so this page owns no context yet. */
    if (slot_page == NULL) return 0;
    module_page = slot_page;
    /* on_resume does not promise a live root on the descriptor, so use the one
     * the page handed to on_create; the firmware keeps it for the page's
     * lifetime either way. */
    return module_page_bind_ui(slot_page, slot_page->root);
}

static int module_page_on_pause(struct canopus_target_page *page)
{
    struct canopus_target_module_page *slot_page = module_page_of(page);

    if (slot_page != NULL && slot_page == module_page) {
        /* The native tree stays; only the owner context goes away, because the
         * page below is frozen for the transition and will re-render on resume. */
        struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
        canopus_ui_host_set_interactive(&slot_page->view, 0);
        if (slot_page->ui != NULL) {
            sup->platform->heap_release(sup->platform_cookie, slot_page->ui);
            slot_page->ui = NULL;
        }
        module_page = NULL;
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
    struct canopus_target_module_page *slot_page = module_page_of(page);
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_target_launcher_entry *launcher = launcher_entry_of(page);

    if (module_page == slot_page && slot_page != NULL &&
        module_page_entry(slot_page, &entry, &ctx) == 0 && entry.on_close != NULL) {
        entry.on_close(ctx, entry.user_data);
    }
    if (slot_page != NULL) {
        module_page_drop_ui(slot_page);
        if (module_page == slot_page) module_page = NULL;
        if (launcher != NULL) {
            uint32_t route = (uint32_t)(page - launcher->pages);
            if (route < launcher->page_count && launcher->module_pages[route] == slot_page) {
                launcher->module_pages[route] = NULL;
                sup->platform->heap_release(sup->platform_cookie, slot_page);
            }
        }
    }
    return 0;
}

/* The module closed its page or is gone: finish the pages it has on the stack.
 * A page below the top is released directly; only the top one needs a finish. */
int canopus_manager_target_module_page_close(uint32_t slot)
{
    struct canopus_target_launcher_entry *entry = NULL;
    struct canopus_target_module_page *page = module_page;
    uint32_t i, route;

    /* Every page this slot owns, deepest last: a paused one is dropped here,
     * the top one goes through page_finish so the firmware unwinds the stack. */
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        if (launcher_entries[i] != NULL && launcher_entries[i]->slot == slot) {
            entry = launcher_entries[i];
            break;
        }
    }
    if (page == NULL || !page->view.active || page->slot != slot) {
        page = NULL;
        if (entry != NULL) {
            for (route = entry->page_count; route > 0u; route--) {
                if (entry->module_pages[route - 1u] != NULL) {
                    page = entry->module_pages[route - 1u];
                    break;
                }
            }
        }
        if (page == NULL || !page->view.active) return 0;
    }
    if (page != module_page) {
        /* A page below the top is not on screen any more; release its own
         * record and let the firmware keep unwinding its stack. */
        module_page_drop_ui(page);
        return 0;
    }
    module_page_finish(page);
    return 1;
}

/* ctx->ui->route for multi-page entries. Forward routes push the destination
 * page through page_goto, which pauses the source and plays the stock push
 * transition; backward routes finish the source so the page below resumes with
 * the pop transition. Both leave the destination as the live top page. */
int canopus_manager_target_module_page_route(uint32_t slot, uint32_t route)
{
    typedef int32_t (*page_goto_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
#ifndef CANOPUS_MGR_BAND11
    typedef int32_t (*page_finish_fn)(void *);
#else /* CANOPUS_MGR_BAND11 */
    typedef void (*page_finish_fn)(uint32_t);
#endif /* CANOPUS_MGR_BAND11 */
    page_goto_fn page_goto = (page_goto_fn)(uintptr_t)FW_ACTIVITY_NAVIGATE;
    struct canopus_target_launcher_entry *entry = NULL;
    struct canopus_target_module_page *page = module_page;
    uint32_t i;

    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        if (launcher_entries[i] != NULL && launcher_entries[i]->slot == slot) {
            entry = launcher_entries[i];
            break;
        }
    }
    if (entry == NULL || entry->page_count == 0u || route >= entry->page_count) {
        return CANOPUS_SVC_ERR_UNSUPPORTED;
    }
    if (page == NULL || !page->view.active || page->slot != slot) {
        return CANOPUS_SVC_ERR_STATE;
    }
    if (page->route == route) return CANOPUS_SVC_OK;
    if (route > page->route) {
        (void)page_goto(((uint32_t)entry->pages[route].app_id << 16) |
                            entry->pages[route].page_id, 0u, 0u, 0u);
        return CANOPUS_SVC_OK;
    }
#ifndef CANOPUS_MGR_BAND11
    (void)((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(&entry->pages[page->route]);
#else /* CANOPUS_MGR_BAND11 */
    ((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
        ((uint32_t)entry->pages[page->route].app_id << 16) |
            entry->pages[page->route].page_id);
#endif /* CANOPUS_MGR_BAND11 */
    return CANOPUS_SVC_OK;
}

/* ---- module launcher entries (ADR-CAN-011) --------------------------- */

#define LAUNCHER_LABEL_FN(n) \
    static const char *launcher_label_##n(void) { return launcher_entries[n]->label; }
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
        if (launcher_entries[i] != NULL && page == &launcher_entries[i]->pages[0]) {
            return launcher_entries[i];
        }
    }
    return NULL;
}

/* Keep the shared bounded copy out of line in the boot-resident image. */
static __attribute__((noinline)) void launcher_copy(char *to, uint32_t size, const char *from)
{
    uint32_t i;

    for (i = 0u; i + 1u < size && from != NULL && from[i] != '\0'; i++) {
        to[i] = from[i];
    }
    to[i] = '\0';
}

int canopus_manager_target_app_publish(uint32_t slot,
                                       const struct canopus_app_entry_v1 *definition)
{
    struct canopus_supervisor_v1 *sup = canopus_supervisor_get();
    struct canopus_target_launcher_entry *entry = NULL;
    uint32_t i;

    if (slot >= CANOPUS_SUP_MODULE_SLOTS || sup->modules[slot].native_app_id == 0u)
        return -22;
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        if (launcher_entries[i] == NULL) break;
    }
    if (i == CANOPUS_LAUNCHER_ENTRIES || sup->platform->heap_alloc == NULL) return -12;
    entry = sup->platform->heap_alloc(sup->platform_cookie, sizeof(*entry));
    if (entry == NULL) return -12;
    canopus_memset(entry, 0, sizeof(*entry));
    launcher_entries[i] = entry;
    launcher_copy(entry->label, sizeof(entry->label), definition->label);
    launcher_copy(entry->icon, sizeof(entry->icon), definition->icon);
    launcher_copy(entry->package, sizeof(entry->package), "com.canopus.module.");
    launcher_copy(entry->package + 19u, sizeof(entry->package) - 19u,
                  (const char *)sup->modules[slot].module_id);
    entry->slot = slot;
    entry->app.app_id = sup->modules[slot].native_app_id;
    entry->native_page_count = definition->page_count;
    for (i = 0u; i < definition->page_count; i++) entry->native_pages[i] = definition->pages[i];
    entry->state = CANOPUS_TARGET_LAUNCHER_QUEUED;
    return 0;
}

int canopus_manager_target_native_app_id_in_use(uint16_t app_id)
{
    typedef void *(*lookup_fn)(uint16_t);
    if (canopus_manager_target_record.identity_result != 0)
        return canopus_manager_target_record.identity_result;
    return ((lookup_fn)(uintptr_t)FW_APP_LOOKUP)(app_id) != NULL;
}

/* Fills the app and page records the firmware is handed at app_install.
 * `page_count` is the module entry's route count: 1 for a single-page entry,
 * above 1 for a multi-page entry whose routes push through the page stack. */
static void launcher_entry_init(struct canopus_target_launcher_entry *entry,
                                uint32_t index, uint32_t page_count)
{
    uint16_t app_id = entry->app.app_id;
    uint32_t route;

    canopus_memset(&entry->app, 0, sizeof(entry->app));
    entry->app.package_name = entry->package;
    entry->app.launcher_icon_resource = entry->icon[0] != '\0' ? entry->icon : launcher_icon;
    entry->app.app_id = app_id;
    entry->app.launcher_metadata_callback = launcher_label_fns[index];
    if (entry->native_page_count != 0u) {
        for (route = 0u; route < entry->native_page_count; route++)
            entry->native_pages[route]->app_id = app_id;
        return;
    }
    if (page_count == 0u) page_count = 1u;
    entry->page_count = page_count;
    /* Register one descriptor per managed UI route so ctx->ui->route can push
     * them through the firmware stack. page_count 0 means the Supervisor's
     * default single page (a module that only ever renders one screen). */
    for (route = 0u; route < page_count; route++) {
        struct canopus_target_page *page = &entry->pages[route];
        canopus_memset(page, 0, sizeof(*page));
        page->page_name = page_name_module_ui;
        page->page_id = (uint16_t)route;
        page->app_id = app_id;
        page->on_signal = manager_on_signal;
        page->on_create = module_page_on_create;
        page->on_resume = module_page_on_resume;
        page->on_pause = module_page_on_pause;
        if (module_page_back_completed_phase() != 0u) {
            page->extension_callback_104 = module_page_system_back;
        }
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
        page->on_destroy = module_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 without V8 records */
        page->on_ui_destroy = module_page_on_ui_destroy;
#endif /* CANOPUS_MGR_BAND11 */
    }
}

static int launcher_failed(struct canopus_target_launcher_entry *entry, int error)
{
    entry->state = CANOPUS_TARGET_LAUNCHER_FAILED;
    canopus_supervisor_get()->modules[entry->slot].native_app_status = CANOPUS_SUP_NATIVE_APP_FAILED;
    return error;
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
        int pending = 0;
        for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
            struct canopus_target_launcher_entry *entry = launcher_entries[i];
            if (entry != NULL && (entry->state == CANOPUS_TARGET_LAUNCHER_QUEUED ||
                                  entry->state == CANOPUS_TARGET_LAUNCHER_INSTALLED)) {
                (void)launcher_failed(entry, canopus_manager_target_record.identity_result);
                pending = 1;
            }
        }
        return pending ? canopus_manager_target_record.identity_result : 0;
    }
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        struct canopus_target_launcher_entry *entry = launcher_entries[i];
        struct canopus_target_page *pages[CANOPUS_TARGET_MODULE_PAGES];
        struct firmware_app_descriptor *installed;
        uint32_t route, count, ui_pages;
        struct canopus_ui_entry_v1 ui_entry;

        if (entry == NULL || entry->state != CANOPUS_TARGET_LAUNCHER_QUEUED) {
            continue;
        }
        /* A reservation belongs to exactly one registration. Never turn a
         * foreign (or already-published) app into a second launcher icon. */
        if (app_lookup(entry->app.app_id) != NULL) {
            return launcher_failed(entry, -17);
        }
        ui_pages = canopus_supervisor_ui_entry(canopus_supervisor_get(),
                                               entry->slot, &ui_entry) == 0
                       ? ui_entry.page_count : 0u;
        launcher_entry_init(entry, i, ui_pages);
        count = entry->native_page_count != 0u ? entry->native_page_count
                                               : entry->page_count;
        for (route = 0u; route < count; route++) {
            pages[route] = entry->native_page_count != 0u
                               ? entry->native_pages[route]
                               : &entry->pages[route];
        }
        (void)app_install(&entry->app, pages, count);
        installed = app_lookup(entry->app.app_id);
        if (installed == NULL || installed->package_name == NULL ||
            strings_differ(installed->package_name, entry->package) != 0) {
            return launcher_failed(entry, -5);
        }
        entry->state = CANOPUS_TARGET_LAUNCHER_INSTALLED;
        return 1;
    }
    for (i = 0u; i < CANOPUS_LAUNCHER_ENTRIES; i++) {
        struct canopus_target_launcher_entry *entry = launcher_entries[i];

        if (entry == NULL || entry->state != CANOPUS_TARGET_LAUNCHER_INSTALLED) {
            continue;
        }
        (void)launcher_add(entry->app.app_id);
        entry->state = CANOPUS_TARGET_LAUNCHER_ADDED;
        canopus_supervisor_get()->modules[entry->slot].native_app_status = CANOPUS_SUP_NATIVE_APP_READY;
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
        manager_pages_desc[page_index].on_ui_destroy = module_page_on_ui_destroy;
#endif /* CANOPUS_MGR_BAND11 */
        return;
    }
    manager_pages_desc[page_index].on_create = manager_page_on_create;
    manager_pages_desc[page_index].on_resume = manager_page_on_resume;
    manager_pages_desc[page_index].on_pause = manager_page_on_pause;
#if !defined(CANOPUS_MGR_BAND11) || defined(CANOPUS_MGR_V8_RECORDS)
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
#else /* CANOPUS_MGR_BAND11 without V8 records */
    manager_pages_desc[page_index].on_ui_destroy = manager_page_on_ui_destroy;
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
