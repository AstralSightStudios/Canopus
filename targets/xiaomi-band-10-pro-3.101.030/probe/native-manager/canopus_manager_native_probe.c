/*
 * Exact-target native Manager registration/UI probe.
 *
 * This deliberately uses fixed-address veneers for firmware 3.101.030 and is
 * resident-first: its constructor only records target identity. The supervisor
 * invokes the exported install entry from a /dev/canopus write made by miwear,
 * then install registers one writable page descriptor and one launcher record.
 * The page creates a stock LVX list row. Device testing must treat reboot as
 * the reliable recovery path until concurrent callback drain is proven.
 */
#include <stddef.h>
#include <stdint.h>

#include "canopus_manager_native_probe.h"

#define CANOPUS_PROBE_APP_ID UINT16_C(0x00CA)
#define CANOPUS_PROBE_PAGE_ID UINT16_C(0)
#define CANOPUS_PROBE_MAGIC UINT32_C(0x434E5031) /* "CNP1" */

#define FW_VERSION_ADDRESS UINT32_C(0x0C0C0810)
#define FW_BUILD_ADDRESS UINT32_C(0x0C0C0850)
#define FW_APP_LOOKUP UINT32_C(0x0CA50FD5)
#define FW_APP_INSTALL UINT32_C(0x0CA519AD)
#define FW_LAUNCHER_ADD UINT32_C(0x0C4F2BDD)
#define FW_LVX_LIST_ROW_CREATE UINT32_C(0x0C52B235)
#define FW_NOTIFICATION_INSERT UINT32_C(0x0CA81F11)

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
static const char page_name[] = "main";
static const char display_name[] = "Canopus Manager";
static const char row_detail[] = "Native UI probe";
static const char notification_title[] = "Canopus";
static const char notification_body[] = "Canpous Loaded! Just ENJOY~";
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

static int manager_on_create(struct firmware_page_descriptor *page, void *root,
                             void *start_data)
{
    typedef void *(*create_row_fn)(void *, const char *, const char *, uint8_t);
    create_row_fn create_row =
        (create_row_fn)(uintptr_t)FW_LVX_LIST_ROW_CREATE;

    (void)page;
    (void)start_data;
    canopus_native_probe_record.create_count += 1u;
    canopus_native_probe_record.root_object = (uintptr_t)root;
    canopus_native_probe_record.list_row =
        (uintptr_t)create_row(root, display_name, row_detail, 3u);
    return canopus_native_probe_record.list_row != 0u ? 0 : -1;
}

static int manager_on_resume(struct firmware_page_descriptor *page)
{
    (void)page;
    canopus_native_probe_record.resume_count += 1u;
    return 0;
}

static int manager_on_pause(struct firmware_page_descriptor *page)
{
    (void)page;
    canopus_native_probe_record.pause_count += 1u;
    return 0;
}

static int manager_on_destroy(struct firmware_page_descriptor *page)
{
    (void)page;
    canopus_native_probe_record.destroy_count += 1u;
    canopus_native_probe_record.list_row = 0u;
    canopus_native_probe_record.root_object = 0u;
    return 0;
}

static struct firmware_page_descriptor manager_page = {
    .page_name = page_name,
    .page_id = CANOPUS_PROBE_PAGE_ID,
    .app_id = CANOPUS_PROBE_APP_ID,
    .on_signal = manager_on_signal,
    .on_create = manager_on_create,
    .on_resume = manager_on_resume,
    .on_pause = manager_on_pause,
    .on_destroy = manager_on_destroy,
};

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

static void __attribute__((constructor, used)) canopus_manager_probe_init(void)
{
    canopus_native_probe_record.magic = CANOPUS_PROBE_MAGIC;
    canopus_native_probe_record.identity_result = identity_guard();
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
    struct firmware_page_descriptor *pages[1];
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

    pages[0] = &manager_page;
    canopus_native_probe_record.app_install_result =
        app_install(&manager_app, pages, 1u);
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
