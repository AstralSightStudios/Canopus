/*
 * Xiaomi Band 9 LVGL v8 Manager target backend.
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
#include "canopus_target_config.h"
#include "canopus_client.h"
#include "canopus_manager_native.h"
#include "canopus_memory.h"
#include "canopus_supervisor.h"

#define CANOPUS_TARGET_PAGE_COUNT 3u
#define CANOPUS_TARGET_PAGE_OVERVIEW 0u
#define CANOPUS_TARGET_PAGE_MODULES 1u
#define CANOPUS_TARGET_PAGE_DETAIL 2u
/* The static page contexts dominate BSS on this target. The exact Kmem heap
 * only leaves ~139 KiB, so a bring-up build can lower the row ceiling without
 * changing the code path. Production stays at 25. */
#ifndef CANOPUS_TARGET_UI_MAX_ROWS
#define CANOPUS_TARGET_UI_MAX_ROWS 25u
#endif
#define CANOPUS_TARGET_UI_MAX_LABELS 6u
#define CANOPUS_TARGET_UI_MAX_IMAGES 2u
#define CANOPUS_TARGET_UI_MAX_PROGRESS 4u
#define CANOPUS_TARGET_MODULE_FLAG_SIGNATURE_OK (1u << 0)
#define CANOPUS_TARGET_ROW_STATUS 1u
#define CANOPUS_TARGET_ROW_ACTION 2u
#define CANOPUS_TARGET_ROW_SWITCH 3u
#define CANOPUS_TARGET_TRAILING_NONE 0u
#define CANOPUS_TARGET_TRAILING_SWITCH 1u
#define CANOPUS_TARGET_TRAILING_FORWARD 12u
#define CANOPUS_TARGET_ALIGN_TOP_MID 2u
#define CANOPUS_TARGET_ALIGN_LEFT_MID 7u
#define CANOPUS_TARGET_ALIGN_RIGHT_MID 8u
#define CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID 14u
#ifndef CANOPUS_TARGET_FONT_FAMILY
#define CANOPUS_TARGET_FONT_FAMILY "MiSans-Regular"
#endif
#ifndef CANOPUS_TARGET_FONT_SIZE
#define CANOPUS_TARGET_FONT_SIZE 14
#endif
#ifndef CANOPUS_TARGET_FONT_VALUE_SIZE
#define CANOPUS_TARGET_FONT_VALUE_SIZE 13
#endif
/* The three v8 panels differ (192x490, 336x480, 212x520); rows are laid out
 * against the real panel width so a right-aligned value stays on screen. */
#ifndef CANOPUS_MGR_SCREEN_WIDTH
#define CANOPUS_MGR_SCREEN_WIDTH 336
#endif
#define CANOPUS_TARGET_CONTENT_TOP_OFFSET 56
#define CANOPUS_TARGET_CONTENT_WIDTH CANOPUS_MGR_SCREEN_WIDTH
#define CANOPUS_TARGET_CONTENT_HEIGHT 424
#define CANOPUS_TARGET_ROW_GAP 2
#define CANOPUS_TARGET_EVENT_ALL 0u
#define CANOPUS_TARGET_EVENT_CLICKED 7u
/* The Mirage touch model releases late, so LVGL reports SHORT_CLICKED (4)
 * instead of CLICKED for a tap on this firmware. */
#define CANOPUS_TARGET_EVENT_SHORT_CLICKED 4u
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
};

struct firmware_app_descriptor {
#if defined(CANOPUS_MGR_APPINFO_190)
    /* 3.1.190 (Band 9 Pro / Band 10) launcher ABI: launcher_load_app_info
     * reads the metadata callback at +0x1c and app_install copies 0x3c
     * bytes, so the 3.1.32 layout (+0x14 callback) calls the null
     * page_registry slot instead. */
    uint32_t registry_prev;                   /* +0x00 */
    uint32_t registry_next;                   /* +0x04 */
    const char *package_name;                 /* +0x08 */
    const char *launcher_icon_resource;       /* +0x0c */
    uint16_t app_id;                          /* +0x10 */
    uint8_t flags;                            /* +0x12 */
    uint8_t _pad_13;                          /* +0x13 */
    const char *owned_string_20;              /* +0x14 */
    const char *owned_string_24;              /* +0x18 */
    const char *(*launcher_metadata_callback)(void); /* +0x1c */
    uint8_t _pad_20[0x18];                   /* +0x20 */
    uint8_t hidden_flags;                     /* +0x38 */
    uint8_t _tail_39[3];                      /* +0x39 */
#elif defined(CANOPUS_MGR_APPINFO_1028)
    /* 3.1.28 (Band 10 o66) launcher ABI: callback at +0x1c, hidden flag at
     * +0x3c, app_install copies 0x40 bytes. */
    uint32_t registry_prev;                   /* +0x00 */
    uint32_t registry_next;                   /* +0x04 */
    const char *package_name;                 /* +0x08 */
    const char *launcher_icon_resource;       /* +0x0c */
    uint16_t app_id;                          /* +0x10 */
    uint8_t flags;                            /* +0x12 */
    uint8_t _pad_13;                          /* +0x13 */
    const char *owned_string_20;              /* +0x14 */
    const char *owned_string_24;              /* +0x18 */
    const char *(*launcher_metadata_callback)(void); /* +0x1c */
    uint8_t _pad_20[0x1c];                   /* +0x20 */
    uint8_t hidden_flags;                     /* +0x3c */
    uint8_t _tail_3d[3];                      /* +0x3d */
#else
    /* 3.1.32 (Band 9 n66) launcher ABI: metadata callback at +0x14,
     * app_install copies 0x2c bytes. */
    uint32_t registry_prev;                   /* +0x00 */
    uint32_t registry_next;                   /* +0x04 */
    const char *package_name;                 /* +0x08 */
    const char *launcher_icon_resource;       /* +0x0c */
    uint16_t app_id;                          /* +0x10 */
    uint8_t flags;                            /* +0x12 */
    uint8_t _pad_13;                         /* +0x13 */
    const char *(*launcher_metadata_callback)(void); /* +0x14 */
    void *launcher_activate_callback;         /* +0x18 */
    void *page_registry;                      /* +0x1c */
    void *secondary_page_registry;            /* +0x20 */
    void *extension_callback;                 /* +0x24 */
    uint8_t hidden_flags;                     /* +0x28 */
    uint8_t _tail_29[3];                     /* +0x29 */
#endif
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
    void *row_labels[CANOPUS_TARGET_UI_MAX_ROWS];
    void *row_values[CANOPUS_TARGET_UI_MAX_ROWS];
    void *labels[CANOPUS_TARGET_UI_MAX_LABELS];
    void *images[CANOPUS_TARGET_UI_MAX_IMAGES];
    void *progress[CANOPUS_TARGET_UI_MAX_PROGRESS];
    uint32_t image_resources[CANOPUS_TARGET_UI_MAX_IMAGES];
    int32_t progress_minimum[CANOPUS_TARGET_UI_MAX_PROGRESS];
    int32_t progress_maximum[CANOPUS_TARGET_UI_MAX_PROGRESS];
    int32_t progress_value[CANOPUS_TARGET_UI_MAX_PROGRESS];
    uint8_t row_kinds[CANOPUS_TARGET_UI_MAX_ROWS];
    canopus_ui_node_id row_keys[CANOPUS_TARGET_UI_MAX_ROWS];
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
static uint32_t manager_pending_detail;
static uint8_t manager_session_ready;

_Static_assert(CANOPUS_MANAGER_TARGET_APP_ID <= UINT16_C(0x00FF),
               "system launch animation requires an 8-bit app id");
_Static_assert(sizeof(struct firmware_page_descriptor) == 112,
               "firmware page descriptor size");
_Static_assert(offsetof(struct firmware_page_descriptor, page_name) == 16,
               "page name offset");
_Static_assert(offsetof(struct firmware_page_descriptor, root_object) == 48,
               "page root offset");
_Static_assert(offsetof(struct firmware_page_descriptor, on_create) == 76,
               "page create offset");
_Static_assert(offsetof(struct firmware_page_descriptor, on_destroy) == 92,
               "page destroy offset");
#if defined(CANOPUS_MGR_APPINFO_190)
_Static_assert(sizeof(struct firmware_app_descriptor) == 60,
               "firmware app descriptor size (3.1.190)");
#elif defined(CANOPUS_MGR_APPINFO_1028)
_Static_assert(sizeof(struct firmware_app_descriptor) == 64,
               "firmware app descriptor size (3.1.28)");
#else
_Static_assert(sizeof(struct firmware_app_descriptor) == 44,
               "firmware app descriptor size");
#endif

static const char package_name[] = "com.canopus.manager";
static const char page_name_overview[] = "main";
static const char page_name_modules[] = "modules";
static const char page_name_detail[] = "module_detail";
static const char display_name[] = "Canopus 管理器";
static const char launcher_icon[] = "/resource/app/calendar/launcher.bin";

__attribute__((used, visibility("default"), section(".data.canopus_manager_target")))
volatile struct canopus_manager_target_record canopus_manager_target_record;

void canopus_manager_target_render_diagnostics(uint8_t out[36])
{
    const volatile uint32_t *source =
        &canopus_manager_target_record.build_id;
    uint32_t i;

    if (out == NULL) return;
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
    const char *version = (const char *)(uintptr_t)FW_VERSION_ADDRESS;
    const char *expected = CANOPUS_TARGET_FIRMWARE_VERSION;

    /* 3.1.32 stores the version as a build.prop line: the token is followed by
     * LF, not NUL. Accept exactly NUL/LF/CR at the token boundary. */
    while (*expected != '\0' && *version == *expected) {
        ++version;
        ++expected;
    }
    if (*expected != '\0') return -1;
    return (*version == '\0' || *version == '\n' || *version == '\r') ? 0 : -1;
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

#if defined(CANOPUS_TARGET_UI_DIAG)
/* Bring-up only: two-byte markers appended to /data/canopus/diag.txt so the
 * click/route sequence can be read back offline. Never shipped. */
/* One file per marker: /data/canopus/dm-<a><b>. A file that exists proves the
 * marker ran, and opening without append would otherwise overwrite the previous
 * marker (which it did in the first version of this probe). */
#define DIAG_DIR "/data/canopus/dm-"

static void diag_name(char a, char b, char *out)
{
    uint32_t i;
    for (i = 0u; i < sizeof(DIAG_DIR) - 1u; i++) {
        out[i] = DIAG_DIR[i];
    }
    out[i] = a;
    out[i + 1u] = b;
    out[i + 2u] = '\0';
}

static void diag_mark(char a, char b)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*close_fn)(int);
    char path[24];
    int fd;

    diag_name(a, b, path);
    fd = ((open_fn)(uintptr_t)FW_NUTTX_OPEN)(path, 2 | 4, 0666);
    if (fd < 0) {
        return;
    }
    (void)((close_fn)(uintptr_t)FW_NUTTX_CLOSE)(fd);
}

static void diag_dump(char a, char b, const void *data, uint32_t len)
{
    typedef int (*open_fn)(const char *, int, ...);
    typedef int (*write_fn)(int, const void *, uint32_t);
    typedef int (*close_fn)(int);
    char path[24];
    int fd;

    if (data == 0 || len > 32u) {
        return;
    }
    diag_name(a, b, path);
    fd = ((open_fn)(uintptr_t)FW_NUTTX_OPEN)(path, 2 | 4, 0666);
    if (fd < 0) {
        return;
    }
    (void)((write_fn)(uintptr_t)FW_NUTTX_WRITE)(fd, data, len);
    (void)((close_fn)(uintptr_t)FW_NUTTX_CLOSE)(fd);
}
#else
#define diag_mark(a, b) ((void)0)
#define diag_dump(a, b, data, len) ((void)0)
#endif

static void target_dispatch_row(uint32_t row_index, void *event)
{
    uint32_t page_index;
    uint32_t code;

    if (event == NULL || row_index >= CANOPUS_TARGET_UI_MAX_ROWS) return;
    diag_dump('E', (char)('0' + (row_index % 10u)), event, 16u);
    code = *(const uint32_t *)((const uint8_t *)event + 8u);
    for (page_index = 0; page_index < CANOPUS_TARGET_PAGE_COUNT; page_index++) {
        struct canopus_target_page_context *context = &manager_pages[page_index];
        struct canopus_target_ui_binding *binding;
        int32_t rc;

        if (!context->active || !context->interactive ||
            context->backend.rows[row_index] == NULL) continue;
        if (code != CANOPUS_TARGET_EVENT_CLICKED &&
            code != CANOPUS_TARGET_EVENT_SHORT_CLICKED) {
            return;
        }
        binding = &context->backend.bindings[row_index];
        if (binding->event_id == 0u) return;
        canopus_manager_target_record.click_count += 1u;
        diag_mark('C', (char)('0' + (row_index % 10u)));
        canopus_manager_target_record.clicked_row = row_index;
        canopus_manager_target_record.clicked_generation = binding->generation;
        canopus_manager_target_record.clicked_key = binding->key;
        canopus_manager_target_record.clicked_event = binding->event_id;
        canopus_manager_target_record.selected_before = manager_model.selected;
        {
            struct canopus_ui_event_v1 event;
            canopus_memset(&event, 0, sizeof(event));
            event.struct_size = sizeof(event);
            event.generation = binding->generation;
            event.key = binding->key;
            event.event_id = binding->event_id;
            event.type = CANOPUS_UI_EVENT_ACTIVATE;
            rc = canopus_ui_dispatch_event(&context->native.ui, &event);
        }
        canopus_manager_target_record.selected_after = manager_model.selected;
        if (rc == CANOPUS_UI_ERR_DISABLED) {
            (void)canopus_manager_native_render(&context->native);
        }
        return;
    }
}

#if defined(CANOPUS_TARGET_UI_DIAG)
/* Does LVGL's indev reach our screen at all? Mark any press that lands here. */
static void diag_root_press(void *event)
{
    uint32_t code = *(const uint32_t *)((const uint8_t *)event + 8u);
    diag_mark('R', (char)('0' + (code % 10u)));
}

static void diag_probe_press(void *event)
{
    uint32_t code = *(const uint32_t *)((const uint8_t *)event + 8u);
    diag_mark('H', (char)('0' + (code % 10u)));
}
#endif

#define CANOPUS_TARGET_ROW_CALLBACK(index) \
    static void target_row_event_##index(void *event) \
    { \
        target_dispatch_row(index, event); \
    }

#if CANOPUS_TARGET_UI_MAX_ROWS > 0
CANOPUS_TARGET_ROW_CALLBACK(0)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 1
CANOPUS_TARGET_ROW_CALLBACK(1)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 2
CANOPUS_TARGET_ROW_CALLBACK(2)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 3
CANOPUS_TARGET_ROW_CALLBACK(3)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 4
CANOPUS_TARGET_ROW_CALLBACK(4)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 5
CANOPUS_TARGET_ROW_CALLBACK(5)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 6
CANOPUS_TARGET_ROW_CALLBACK(6)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 7
CANOPUS_TARGET_ROW_CALLBACK(7)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 8
CANOPUS_TARGET_ROW_CALLBACK(8)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 9
CANOPUS_TARGET_ROW_CALLBACK(9)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 10
CANOPUS_TARGET_ROW_CALLBACK(10)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 11
CANOPUS_TARGET_ROW_CALLBACK(11)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 12
CANOPUS_TARGET_ROW_CALLBACK(12)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 13
CANOPUS_TARGET_ROW_CALLBACK(13)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 14
CANOPUS_TARGET_ROW_CALLBACK(14)
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 15
CANOPUS_TARGET_ROW_CALLBACK(15)
#endif

static void (*const target_row_events[CANOPUS_TARGET_UI_MAX_ROWS])(void *) = {
#if CANOPUS_TARGET_UI_MAX_ROWS > 0
    target_row_event_0,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 1
    target_row_event_1,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 2
    target_row_event_2,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 3
    target_row_event_3,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 4
    target_row_event_4,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 5
    target_row_event_5,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 6
    target_row_event_6,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 7
    target_row_event_7,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 8
    target_row_event_8,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 9
    target_row_event_9,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 10
    target_row_event_10,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 11
    target_row_event_11,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 12
    target_row_event_12,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 13
    target_row_event_13,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 14
    target_row_event_14,
#endif
#if CANOPUS_TARGET_UI_MAX_ROWS > 15
    target_row_event_15,
#endif
};

#undef CANOPUS_TARGET_ROW_CALLBACK

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

static int target_find_row(struct canopus_target_ui_backend *backend,
                           const struct canopus_ui_snapshot_v1 *snapshot,
                           uint8_t kind, canopus_ui_node_id key,
                           uint32_t used_mask)
{
    uint32_t i;
    int reusable = -1;
    int empty = -1;

    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] == NULL) {
            if (empty < 0) empty = (int)i;
        } else if (backend->row_kinds[i] == kind &&
                   (used_mask & (UINT32_C(1) << i)) == 0u) {
            if (backend->row_keys[i] == key) return (int)i;
            if (reusable < 0 && !target_snapshot_uses_row(
                    snapshot, kind, backend->row_keys[i])) {
                reusable = (int)i;
            }
        }
    }
    return reusable >= 0 ? reusable : empty;
}


/* LVGL v8 flags: CLICKABLE is bit 1, HIDDEN bit 0, SCROLLABLE bit 4. A plain
 * lv_obj is not clickable, so a row only receives LV_EVENT_CLICKED once this
 * flag is set. */
#define CANOPUS_TARGET_FLAG_CLICKABLE UINT32_C(0x02)
#define CANOPUS_TARGET_FLAG_SCROLLABLE UINT32_C(0x10)
#define CANOPUS_TARGET_FLAG_HIDDEN UINT32_C(0x01)

static void target_set_flag(void *object, uint32_t flag, uint32_t on)
{
    typedef void (*flag_fn)(void *, uint32_t);
    flag_fn set_flag = (flag_fn)(uintptr_t)(on != 0u
        ? FW_LV_OBJECT_ADD_FLAG : FW_LV_OBJECT_CLEAR_FLAG);
    if (object != NULL) set_flag(object, flag);
}

static void target_set_hidden(void *object, uint32_t hidden)
{
    target_set_flag(object, CANOPUS_TARGET_FLAG_HIDDEN, hidden);
}

/* Objects made on a fresh screen do not inherit the theme's text font, so a
 * Chinese label renders as .notdef boxes. Stock pages read the font off a
 * global holder; here the same family is created once and applied per label. */
#define CANOPUS_LABEL_BODY 0u
#define CANOPUS_LABEL_VALUE 1u

#define CANOPUS_TARGET_ROW_TEXT_MAX 25u

struct row_text {
    char data[CANOPUS_TARGET_ROW_TEXT_MAX];
    uint32_t used;
};

static void row_text_begin(struct row_text *text)
{
    text->used = 0u;
    text->data[0] = '\0';
}

static void row_text_append(struct row_text *text, const char *value,
                            uint32_t len)
{
    uint32_t i = 0u;
    while (i < len && text->used + 1u < sizeof(text->data)) {
        text->data[text->used++] = value[i++];
    }
    /* A cut must not leave half a UTF-8 sequence: the font engine would read it
     * as one more glyph. */
    if (i < len) {
        while (text->used != 0u &&
               ((unsigned char)text->data[text->used - 1u] & 0xC0u) == 0x80u) {
            text->used--;
        }
        if (text->used != 0u &&
            ((unsigned char)text->data[text->used - 1u] & 0xC0u) == 0xC0u) {
            text->used--;
        }
    }
    text->data[text->used] = '\0';
}


static void *manager_label_create(void *parent, uint32_t role)
{
    void *label = ((void *(*)(void *))(uintptr_t)FW_LV_LABEL_CREATE)(parent);
    if (label == NULL) {
        return NULL;
    }
#if defined(FW_LVX_FONT_CREATE)
    {
        static void *fonts[2];
        int16_t size = role == CANOPUS_LABEL_VALUE
                           ? CANOPUS_TARGET_FONT_VALUE_SIZE
                           : CANOPUS_TARGET_FONT_SIZE;
        if (fonts[role] == NULL) {
            fonts[role] = ((void *(*)(const char *, int16_t, int16_t))(uintptr_t)
                               FW_LVX_FONT_CREATE)(CANOPUS_TARGET_FONT_FAMILY,
                                                   size, 0);
        }
        if (fonts[role] != NULL) {
            (void)((int32_t (*)(void *, void *, uint32_t))(uintptr_t)
                       FW_LV_OBJECT_SET_TEXT_FONT)(label, fonts[role], 0u);
        }
    }
#else
    (void)role;
#endif
    return label;
}

static int32_t target_ui_apply(
    void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    /* v8 forwards its second argument (0 here) to lv_obj_class_init_obj. */
    typedef void *(*object_create_fn)(void *, void *);
    typedef void (*set_label_text_fn)(void *, const char *);
    typedef void *(*create_page_title_fn)(void *, const char *, uint32_t,
                                          void (*)(void *));
    typedef void (*add_event_fn)(void *, void (*)(void *), uint32_t, void *);
    typedef void (*align_to_fn)(void *, void *, uint32_t, int32_t, int32_t);
    typedef void (*set_size_fn)(void *, int32_t, int32_t);
    typedef void *(*image_create_fn)(void *);
    typedef void (*image_set_src_fn)(void *, const void *);
    typedef void *(*bar_create_fn)(void *);
    typedef void (*bar_set_range_fn)(void *, int32_t, int32_t);
    typedef void (*bar_set_value_fn)(void *, int32_t, uint32_t);
    struct canopus_target_ui_backend *backend =
        (struct canopus_target_ui_backend *)cookie;
    object_create_fn object_create =
        (object_create_fn)(uintptr_t)FW_LV_OBJECT_CREATE;
    set_label_text_fn set_label_text =
        (set_label_text_fn)(uintptr_t)FW_LV_LABEL_SET_TEXT;
    create_page_title_fn create_page_title =
        (create_page_title_fn)(uintptr_t)FW_LV_PAGE_TITLE_CREATE;
    add_event_fn add_event = (add_event_fn)(uintptr_t)FW_LV_EVENT_ADD;
    align_to_fn align_to = (align_to_fn)(uintptr_t)FW_LV_ALIGN_TO;
    set_size_fn set_size = (set_size_fn)(uintptr_t)FW_LV_OBJECT_SET_SIZE;
    image_create_fn image_create =
        (image_create_fn)(uintptr_t)FW_LV_IMAGE_CREATE;
    image_set_src_fn image_set_src =
        (image_set_src_fn)(uintptr_t)FW_LV_IMAGE_SET_SRC;
    bar_create_fn bar_create = (bar_create_fn)(uintptr_t)FW_LV_BAR_CREATE;
    bar_set_range_fn bar_set_range =
        (bar_set_range_fn)(uintptr_t)FW_LV_BAR_SET_RANGE;
    bar_set_value_fn bar_set_value =
        (bar_set_value_fn)(uintptr_t)FW_LV_BAR_SET_VALUE;
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
    struct row_text row_text;

    if (backend == NULL || backend->root == NULL || snapshot == NULL ||
        snapshot->node_count == 0u ||
        backend->page_index >= CANOPUS_TARGET_PAGE_COUNT) {
        return -1;
    }
    if (backend->content_root == NULL) {
        backend->content_root = backend->root;
    }
    for (i = 0; i < snapshot->node_count; i++) {
        uint16_t type = snapshot->nodes[i].type;
        if (type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
            continue;
        }
        /* A section is only a grouping container in the semantic tree; this
         * firmware has no recovered section widget, so its title is drawn as
         * an ordinary label. */
        if (type == CANOPUS_UI_NODE_SECTION || type == CANOPUS_UI_NODE_TEXT) {
            visible_labels++;
            continue;
        }
        if (type == CANOPUS_UI_NODE_IMAGE) {
            visible_images++;
            continue;
        }
        if (type == CANOPUS_UI_NODE_PROGRESS) {
            visible_progress++;
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
        visible_rows > CANOPUS_TARGET_UI_MAX_ROWS ||
        visible_images > CANOPUS_TARGET_UI_MAX_IMAGES ||
        visible_progress > CANOPUS_TARGET_UI_MAX_PROGRESS) {
        return -1;
    }

    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        const char *primary;
        uint8_t kind;
        int slot;
        void *object;
        int32_t gap = CANOPUS_TARGET_ROW_GAP;

        primary = snapshot->strings + node->primary_off;
        if (node->type == CANOPUS_UI_NODE_NAVIGATION_PAGE) {
            uint32_t title_mode = backend->page_index ==
                CANOPUS_TARGET_PAGE_OVERVIEW ? 0u : 1u;

            if (backend->page_title == NULL) {
                diag_mark('T', '0');
                backend->page_title = create_page_title(
                    backend->root, primary, title_mode, NULL);
                if (backend->page_title == NULL) {
                    return -1;
                }
                diag_mark('T', '1');
            }
            /* Band 9 lvx_page_title_create owns the stock left title, right-side time,
             * optional back affordance, and its standard content inset. It is
             * intentionally created once per firmware page: updating its title
             * contract is not yet recovered. */
            target_set_hidden(backend->page_title, 0u);
#if defined(CANOPUS_TARGET_UI_DIAG)
            {
                /* A plain clickable object at a known place: if it never sees a
                 * press either, the hit test of our whole screen is the issue,
                 * not the rows. */
                typedef void (*add_event_fn)(void *, void (*)(void *), uint32_t,
                                             void *);
                void *probe = object_create(backend->root, 0);
                ((add_event_fn)(uintptr_t)FW_LV_EVENT_ADD)(backend->root,
                                                          diag_root_press, 0u,
                                                          NULL);
                target_set_flag(backend->root, CANOPUS_TARGET_FLAG_CLICKABLE, 1u);
                if (probe != NULL) {
                    set_size(probe, 80, 80);
                    target_set_flag(probe, CANOPUS_TARGET_FLAG_CLICKABLE, 1u);
                    add_event(probe, diag_probe_press, CANOPUS_TARGET_EVENT_ALL,
                              NULL);
                    align_to(probe, backend->root,
                             CANOPUS_TARGET_ALIGN_TOP_MID, 0, 200);
                }
            }
#endif
            previous = backend->page_title;
            first = backend->page_title;
            continue;
        }
        if (node->type == CANOPUS_UI_NODE_TEXT ||
            node->type == CANOPUS_UI_NODE_SECTION) {
            object = backend->labels[label_used];
            if (object == NULL) {
                object = manager_label_create(backend->content_root, CANOPUS_LABEL_BODY);
                if (object == NULL) {
                    return -1;
                }
                backend->labels[label_used] = object;
                backend->label_count++;
            }
            set_label_text(object, primary);
            target_set_hidden(object, 0u);
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
            target_set_hidden(object, 0u);
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
        if (node->type == CANOPUS_UI_NODE_PROGRESS) {
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
            target_set_hidden(object, 0u);
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
        kind = target_row_kind(node->type);
        slot = target_find_row(backend, snapshot, kind, node->key,
                               used_mask);
        if (slot < 0) {
            return -1;
        }
        object = backend->rows[slot];
        if (object == NULL) {
            void *label;
            void *value;
            diag_mark('B', '0');
            object = object_create(backend->content_root, 0);
            if (object == NULL) {
                return -1;
            }
            diag_mark('B', '1');
            label = manager_label_create(object, CANOPUS_LABEL_BODY);
            diag_mark('L', label != NULL ? '1' : '0');
            value = manager_label_create(object, CANOPUS_LABEL_VALUE);
            diag_mark('V', value != NULL ? '1' : '0');
            if (label == NULL || value == NULL) {
                return -1;
            }
            set_size(object, CANOPUS_TARGET_CONTENT_WIDTH, 34);
            target_set_flag(object, CANOPUS_TARGET_FLAG_CLICKABLE, 1u);
            target_set_flag(object, CANOPUS_TARGET_FLAG_SCROLLABLE, 0u);
            add_event(object, target_row_events[slot],
                      CANOPUS_TARGET_EVENT_ALL, NULL);
            backend->rows[slot] = object;
            backend->row_labels[slot] = label;
            backend->row_values[slot] = value;
            backend->row_kinds[slot] = kind;
            backend->row_count++;
        }
        /* Stock row: title left, value right. The panel is 192-212 px wide, so
         * the value is capped to the characters that fit beside the title
         * instead of running over it. Alignment follows the text because a
         * label only knows its size once it holds text. */
        diag_mark('S', '0');
        set_label_text(backend->row_labels[slot], primary);
        diag_mark('S', '1');
        if (node->type == CANOPUS_UI_NODE_ACTION_ROW) {
            /* An action row's secondary text is a hint too long for the value
             * column; the chevron is what stock rows show instead. */
            set_label_text(backend->row_values[slot], "\xE2\x80\xBA");
        } else {
            row_text_begin(&row_text);
            row_text_append(&row_text, snapshot->strings + node->secondary_off,
                            node->secondary_len);
            set_label_text(backend->row_values[slot], row_text.data);
        }
        align_to(backend->row_labels[slot], object,
                 CANOPUS_TARGET_ALIGN_LEFT_MID, 8, 0);
        align_to(backend->row_values[slot], object,
                 CANOPUS_TARGET_ALIGN_RIGHT_MID, -8, 0);
        target_set_hidden(object, 0u);
        diag_mark('A', '0');
        if (previous == NULL) {
            align_to(object, backend->content_root,
                     CANOPUS_TARGET_ALIGN_TOP_MID, 0, 0);
            first = object;
        } else {
            align_to(object, previous, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0,
                     gap);
        }
        diag_mark('A', '1');
        previous = object;
        backend->row_keys[slot] = node->key;
        backend->bindings[slot].generation = snapshot->generation;
        backend->bindings[slot].key = node->key;
        backend->bindings[slot].event_id = node->event_id;
        used_mask |= UINT32_C(1) << (uint32_t)slot;
    }
    for (i = 0; i < CANOPUS_TARGET_UI_MAX_ROWS; i++) {
        if (backend->rows[i] != NULL &&
            (used_mask & (UINT32_C(1) << i)) == 0u) {
            target_set_hidden(backend->rows[i], 1u);
            canopus_memset(&backend->bindings[i], 0,
                           sizeof(backend->bindings[i]));
        }
    }
    for (i = (uint16_t)label_used; i < CANOPUS_TARGET_UI_MAX_LABELS; i++) {
        if (backend->labels[i] != NULL) target_set_hidden(backend->labels[i], 1u);
    }
    for (i = (uint16_t)image_used; i < CANOPUS_TARGET_UI_MAX_IMAGES; i++) {
        if (backend->images[i] != NULL) target_set_hidden(backend->images[i], 1u);
    }
    for (i = (uint16_t)progress_used; i < CANOPUS_TARGET_UI_MAX_PROGRESS; i++) {
        if (backend->progress[i] != NULL) target_set_hidden(backend->progress[i], 1u);
    }
    diag_mark('Z', '1');
    backend->rendered_generation = snapshot->generation;
    canopus_manager_target_record.list_row = (uintptr_t)first;
    return 0;
}

/* Only the stock page title is implemented here: lvx_page_title_create owns the
 * header, and no v8 style/font/colour entry is recovered, so the semantic
 * styles and the message-box component stay undeclared. */
static const struct canopus_ui_backend_v1 target_ui_backend_api = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    CANOPUS_UI_CAP_NAVIGATION_HEADER,
    target_ui_apply,
    0,
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
    /* A push can hand on_create a copy of the registered descriptor, so fall
     * back to the (app id, page id) pair the registry keys pages by. */
    if (page->app_id == CANOPUS_MANAGER_TARGET_APP_ID &&
        (uint32_t)page->page_id < CANOPUS_TARGET_PAGE_COUNT) {
        return &manager_pages[page->page_id];
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
#if defined(CANOPUS_MGR_DEFER_ROUTES) && defined(FW_LV_TIMER_CREATE) && defined(FW_LV_TIMER_DEL)
/* The push runs one LVGL timer tick later, not inside the click callback:
 * page_goto runs the page transition synchronously and the transition tears the
 * source page down, so returning into the input dispatch afterwards touched a
 * context that on_destroy had just cleared (device reset on o66). */
static uint32_t pending_route_key;
static uint32_t pending_route_detail;
static uint32_t pending_route_finish = CANOPUS_TARGET_PAGE_COUNT;

static void target_route_push(void *timer)
{
    typedef void (*timer_del_fn)(void *);
    typedef int32_t (*page_goto_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    typedef int32_t (*page_finish_fn)(void *);
    uint32_t key = pending_route_key;
    uint32_t detail = pending_route_detail;
    uint32_t finish = pending_route_finish;

    pending_route_key = 0u;
    pending_route_finish = CANOPUS_TARGET_PAGE_COUNT;
    ((timer_del_fn)(uintptr_t)FW_LV_TIMER_DEL)(timer);
    if (finish < CANOPUS_TARGET_PAGE_COUNT) {
        (void)((page_finish_fn)(uintptr_t)FW_ACTIVITY_FINISH)(
            &manager_pages_desc[finish]);
    } else if (key != 0u) {
        int32_t rc = ((page_goto_fn)(uintptr_t)FW_ACTIVITY_NAVIGATE)(
            key, detail, 0u, 0u);
        (void)rc;
        diag_mark(rc < 0 ? 'X' : 'K', 'P');
    }
}

static int target_route_schedule(uint32_t key, uint32_t detail,
                                 uint32_t finish)
{
    typedef void *(*timer_create_fn)(void (*)(void *), uint32_t, void *);
    pending_route_key = key;
    pending_route_detail = detail;
    pending_route_finish = finish;
    if (((timer_create_fn)(uintptr_t)FW_LV_TIMER_CREATE)(target_route_push, 1u,
                                                        NULL) != NULL) {
        return 0;
    }
    if (finish < CANOPUS_TARGET_PAGE_COUNT) {
        (void)((int32_t (*)(void *))(uintptr_t)FW_ACTIVITY_FINISH)(
            &manager_pages_desc[finish]);
    } else if (key != 0u) {
        (void)((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)
                   FW_ACTIVITY_NAVIGATE)(key, detail, 0u, 0u);
    }
    return 0;
}

#else
/* Only the 3.1.28 target has verified lv_timer_create/lv_timer_del entries, so
 * only it defers the push. The other v8 targets push inline, which is safe for
 * OPEN_MANAGER (no source page) but must be checked per firmware for routes. */
static int target_route_schedule(uint32_t key, uint32_t detail,
                                 uint32_t finish)
{
    if (finish < CANOPUS_TARGET_PAGE_COUNT) {
        (void)((int32_t (*)(void *))(uintptr_t)FW_ACTIVITY_FINISH)(
            &manager_pages_desc[finish]);
    } else if (key != 0u) {
        (void)((int32_t (*)(uint32_t, uint32_t, uint32_t, uint32_t))(uintptr_t)
                   FW_ACTIVITY_NAVIGATE)(key, detail, 0u, 0u);
    }
    return 0;
}
#endif

static int32_t target_route(void *cookie,
                            struct canopus_manager_native_v1 *native,
                            uint32_t route)
{
    struct canopus_target_page_context *source;
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
        target_route_schedule(0u, 0u, source_page);
    } else {
        target_route_schedule(target_page_key(target_page),
                              target_page == CANOPUS_TARGET_PAGE_DETAIL
                                  ? manager_pending_detail : 0u,
                              CANOPUS_TARGET_PAGE_COUNT);
    }
    /* A push pauses the source. Rendering the destination snapshot into that
     * paused root caused the old two-tap/mixed-content defect. */
    return CANOPUS_UI_OK;
}

static int manager_page_on_create(struct firmware_page_descriptor *page,
                                  void *root, void *start_data)
{
    struct canopus_target_page_context *context;
    uint32_t requested_detail = 0u;
    int32_t rc;

    context = context_for_page(page);
    if (context == NULL) {
        diag_mark('c', '0');
        return -1;
    }
    diag_mark('c', '1');
    if ((uint32_t)page->page_id == CANOPUS_TARGET_PAGE_DETAIL) {
        requested_detail = (uint32_t)(uintptr_t)start_data;
        if (requested_detail == 0u) {
            requested_detail = manager_pending_detail;
        }
    }
    canopus_manager_target_record.create_count += 1u;
    {
        /* Always own the root. lvx_page_title_create on the page manager's own
         * container never returns on 3.1.32, and a screen of our own carries the
         * stock title and the rows on every v8 firmware. */
        typedef void *(*object_create_fn)(void *, void *);
        object_create_fn object_create =
            (object_create_fn)(uintptr_t)FW_LV_OBJECT_CREATE;
        (void)root;
        root = object_create(NULL, 0);
        if (root == NULL) {
            diag_mark('r', '0');
            return -1;
        }
        diag_mark('r', '1');
        /* A scrollable screen turns every tap into SCROLL_BEGIN, which
         * suppresses LV_EVENT_CLICKED for the rows (diag: probe got
         * SHORT_CLICKED and SCROLL_BEGIN but never CLICKED). */
        target_set_flag(root, CANOPUS_TARGET_FLAG_SCROLLABLE, 0u);
    }
    canopus_manager_target_record.root_object = (uintptr_t)root;
    canopus_memset(&context->backend, 0, sizeof(context->backend));
    context->backend.root = root;
    context->backend.firmware_page = page;
    context->backend.page_index = (uint8_t)page->page_id;
    /* Restore only after the supervisor's stock `insmod` constructor returns;
     * nested module loads need the regular page-owner task's stack. */
    (void)canopus_supervisor_restore_after_boot();
    if (!manager_session_ready) {
        rc = canopus_client_init(&manager_client, &target_device_io, NULL);
        diag_mark('l', rc == CANOPUS_CLIENT_OK ? '1' : '0');
        if (rc != CANOPUS_CLIENT_OK ||
            canopus_client_open(&manager_client) != CANOPUS_CLIENT_OK) {
            diag_mark('o', '0');
            return -1;
        }
        diag_mark('o', '1');
        canopus_manager_init(&manager_model, canopus_client_transport,
                             &manager_client);
        canopus_manager_set_identity(
            &manager_model, CANOPUS_TARGET_ID, CANOPUS_TARGET_FIRMWARE_VERSION,
            CANOPUS_TARGET_FIRMWARE_BUILD, 1u);
        if (target_refresh_model() != 0) {
            diag_mark('q', '0');
            (void)canopus_client_close(&manager_client);
            manager_client.fd = -1;
            return -1;
        }
        diag_mark('q', '1');
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
        diag_mark('v', '0');
        return -1;
    }
    diag_mark('v', '1');
    if (context->backend.page_index == CANOPUS_TARGET_PAGE_DETAIL) {
        manager_pending_detail = 0u;
    }
    rc = canopus_manager_native_init(&context->native, &manager_model,
                                     &target_ui_backend_api, &context->backend);
    if (rc != CANOPUS_UI_OK) {
        diag_mark('n', (char)('0' + (rc < 0 && rc > -10 ? -rc : 9)));
        return -1;
    }
    diag_mark('n', '1');
    canopus_manager_native_set_router(&context->native, target_route, NULL);
    canopus_manager_native_set_refresh(&context->native, target_refresh,
                                       context);
    if (!context->active) {
        context->active = 1u;
        manager_active_pages++;
    }
    context->interactive = 1u;
    diag_mark('I', (char)('0' + (context->backend.page_index % 10u)));
    return 0;
}

static int manager_page_on_resume(struct firmware_page_descriptor *page)
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

static int manager_page_on_pause(struct firmware_page_descriptor *page)
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

static int manager_page_on_destroy(struct firmware_page_descriptor *page)
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
    .app_id = CANOPUS_MANAGER_TARGET_APP_ID,
    .launcher_metadata_callback = manager_display_name,
#if defined(CANOPUS_MGR_APPINFO_1028) || !defined(CANOPUS_MGR_APPINFO_190)
    /* launcher_load_app_info copies bit 1 of this byte into the launcher
     * entry's "show in app list" flag (entry+16, bit 2); the launcher's draw
     * loop skips every entry without it. Verified on 3.1.28 (byte +0x3C) and
     * 3.1.32 (byte +0x28); the 3.1.190 layout is not verified yet. Bit 0
     * (hidden) must stay 0. */
    .hidden_flags = 0x02,
#endif
};

int canopus_manager_native_notify_module_installed(void)
{
    return 0;
}

static void __attribute__((constructor, used)) canopus_manager_target_init(void)
{
    canopus_manager_target_record.magic = CANOPUS_MANAGER_TARGET_MAGIC;
    canopus_manager_target_record.build_id = CANOPUS_MANAGER_TARGET_BUILD_ID;
    canopus_manager_target_record.identity_result = identity_guard();
    diag_mark('S', 'T');
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
    manager_pages_desc[page_index].on_destroy = manager_page_on_destroy;
}

int canopus_manager_native_install(void)
{
    typedef int (*app_install_fn)(const struct firmware_app_descriptor *,
                                  struct firmware_page_descriptor *const *,
                                  uint32_t);
    typedef void *(*app_lookup_fn)(uint16_t);
    typedef void *(*launcher_load_fn)(void *);
    typedef int (*launcher_insert_fn)(void *);
    struct firmware_page_descriptor *pages[CANOPUS_TARGET_PAGE_COUNT];
    app_install_fn app_install = (app_install_fn)(uintptr_t)FW_APP_INSTALL;
    app_lookup_fn app_lookup = (app_lookup_fn)(uintptr_t)FW_APP_LOOKUP;
    launcher_load_fn launcher_load =
        (launcher_load_fn)(uintptr_t)FW_LAUNCHER_LOAD_APP_INFO;
    launcher_insert_fn launcher_insert =
        (launcher_insert_fn)(uintptr_t)FW_LAUNCHER_PAGE_INSERT_ICON;
    void *installed_app;
    void *launcher_item;

    if (canopus_manager_target_record.identity_result != 0) {
        return canopus_manager_target_record.identity_result;
    }
    installed_app = app_lookup(CANOPUS_MANAGER_TARGET_APP_ID);
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
    canopus_manager_target_record.app_install_result =
        app_install(&manager_app, pages, CANOPUS_TARGET_PAGE_COUNT);
    installed_app = app_lookup(CANOPUS_MANAGER_TARGET_APP_ID);
    if (installed_app == NULL) {
        canopus_manager_target_record.app_install_result = -100;
        return -100;
    }
    launcher_item = launcher_load(installed_app);
    canopus_manager_target_record.launcher_add_result = launcher_item != NULL
        ? launcher_insert(launcher_item) : -102;
    /* launcher_reset_order_info (o66 0xc4fc508) is deliberately not called:
     * the firmware logs "[launcher] launcher_page_insert_icon: launcher UI not
     * created" when the installer runs from the watchface page, and calling the
     * order reset in that state makes the next launcher open reset the device
     * (observed on Mirage o66; removing the call stops it). The order metadata
     * is left for the launcher's own rebuild. */
    canopus_manager_target_record.notification_result = 0;
    return 0;
}

/* ---- platform service stubs (LVGL v8) -------------------------------
 * The Band 9 Manager backend predates ADR-CAN-011 module launcher entries
 * and the callback-driven notification helper. Keep the Supervisor service
 * surface linkable and fail closed until the v8 implementations exist.
 * These functions are honest negatives: no page is closed, no launcher
 * entry is published and no notification is posted. */
int canopus_manager_target_module_page_close(uint32_t slot)
{
    (void)slot;
    return 0;
}

int canopus_manager_target_launcher_publish(uint32_t slot, const char *label,
                                            const char *icon)
{
    (void)slot;
    (void)label;
    (void)icon;
    return -1;
}

int canopus_manager_target_launcher_step(void)
{
    return 0;
}

int canopus_manager_native_notify(const char *source, const char *title,
                                  const char *body, const char *icon)
{
    (void)source;
    (void)title;
    (void)body;
    (void)icon;
    return -1;
}

int canopus_manager_native_open(void)
{
    typedef int32_t (*page_goto_fn)(uint32_t, uint32_t, uint32_t, uint32_t);
    page_goto_fn page_goto = (page_goto_fn)(uintptr_t)FW_ACTIVITY_NAVIGATE;

    if (canopus_manager_target_record.identity_result != 0) return -1;
#if defined(CANOPUS_TARGET_UI_DIAG) && defined(FW_PAGE_LOOKUP)
    {
        /* Which key shape does the page registry hold? 'F<idx>Y/N'. */
        typedef void *(*lookup_fn)(uint32_t);
        static const uint32_t keys[4] = {
            ((uint32_t)CANOPUS_MANAGER_TARGET_APP_ID << 16) | 0u,
            ((uint32_t)CANOPUS_MANAGER_TARGET_APP_ID << 8) | 0u,
            (uint32_t)CANOPUS_MANAGER_TARGET_APP_ID,
            ((uint32_t)CANOPUS_MANAGER_TARGET_APP_ID << 16) | 1u};
        uint32_t i;
        for (i = 0u; i < 4u; i++) {
            void *node = ((lookup_fn)(uintptr_t)FW_PAGE_LOOKUP)(keys[i]);
            diag_mark('F', (char)('0' + i));
            diag_mark(node != NULL ? 'Y' : 'N', (char)('0' + i));
        }
    }
#endif
    /* Opening the Manager has no source page of our own, so the push is safe to
     * run inline; only row routes have to wait for the next tick. */
    return page_goto(target_page_key(CANOPUS_TARGET_PAGE_OVERVIEW), 0u, 0u, 0u) < 0
               ? -1 : 0;
}
