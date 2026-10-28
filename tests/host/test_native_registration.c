/* Run the real target registration code with the page fixture's UI stubs.
 * Only the firmware registry/install/launcher boundary is modeled by Unicorn;
 * identity allocation belongs to the separate Supervisor host tests. */
#include "test_module_page_back.c"

static struct canopus_target_page fixture_native_pages[2];
static uintptr_t fixture_source_pages;
static struct canopus_target_launcher_entry fixture_registration_arena[CANOPUS_LAUNCHER_ENTRIES];
static uint32_t fixture_registration_allocations;

static void *fixture_registration_alloc(void *cookie, uint32_t size)
{
    (void)cookie;
    if (size != sizeof(fixture_registration_arena[0]) ||
        fixture_registration_allocations == CANOPUS_LAUNCHER_ENTRIES) return NULL;
    return &fixture_registration_arena[fixture_registration_allocations++];
}

static void fixture_registration_free(void *cookie, void *ptr)
{
    (void)cookie;
    (void)ptr;
}

static const struct canopus_sup_platform_v1 fixture_registration_platform = {
    .heap_alloc = fixture_registration_alloc,
    .heap_release = fixture_registration_free,
};

static int fixture_native_create(struct canopus_target_page *page, void *root, void *data)
{
    (void)page;
    (void)root;
    (void)data;
    return 0;
}

void test_registration_prepare(void)
{
    (void)test_prepare(0u, 0u);
    fixture_supervisor.platform = &fixture_registration_platform;
    fixture_registration_allocations = 0u;
    canopus_memset(fixture_registration_arena, 0, sizeof(fixture_registration_arena));
    canopus_memset(launcher_entries, 0, sizeof(launcher_entries));
    canopus_memset(fixture_native_pages, 0, sizeof(fixture_native_pages));
    canopus_manager_target_record.identity_result = 0;
    fixture_supervisor.modules[3].native_app_id = 0xd3u;
    launcher_copy((char *)fixture_supervisor.modules[3].module_id,
                  sizeof(fixture_supervisor.modules[3].module_id), "corona");
    fixture_supervisor.modules[7].state = CANOPUS_STATE_ACTIVE;
    fixture_supervisor.modules[7].native_app_id = 0xccu;
    launcher_copy((char *)fixture_supervisor.modules[7].module_id,
                  sizeof(fixture_supervisor.modules[7].module_id), "native_player");
    fixture_native_pages[0].page_name = "player_main";
    fixture_native_pages[1].page_name = "player_settings";
    fixture_native_pages[0].page_id = 7u;
    fixture_native_pages[1].page_id = 11u;
    fixture_native_pages[0].app_id = fixture_native_pages[1].app_id = 0xbadu;
    fixture_native_pages[0].on_create = fixture_native_pages[1].on_create = fixture_native_create;
    fixture_source_pages = 0u;
}

int test_publish_ui(void)
{
    struct canopus_app_entry_v1 entry = {
        sizeof(entry), "Corona", "/data/corona/logo.bin", NULL, 0u
    };
    int rc = canopus_manager_target_app_publish(3u, &entry);
    if (rc == 0) fixture_supervisor.modules[3].native_app_status = CANOPUS_SUP_NATIVE_APP_PENDING;
    return rc;
}

int test_publish_native(void)
{
    void *volatile pages[2] = {&fixture_native_pages[0], &fixture_native_pages[1]};
    struct canopus_app_entry_v1 entry = {
        sizeof(entry), "Player", "/data/player/logo.bin", (void *const *)pages, 2u
    };
    int rc;
    fixture_source_pages = (uintptr_t)pages;
    rc = canopus_manager_target_app_publish(7u, &entry);
    /* The request and pointer array may disappear immediately after return.
     * Volatile stores ensure this invalidation actually reaches ARM memory. */
    pages[0] = NULL;
    pages[1] = NULL;
    if (rc == 0) fixture_supervisor.modules[7].native_app_status = CANOPUS_SUP_NATIVE_APP_PENDING;
    return rc;
}

int test_registration_step(void) { return canopus_manager_target_launcher_step(); }
uint32_t test_registration_status(uint32_t slot)
{ return fixture_supervisor.modules[slot].native_app_status; }
void test_registration_identity_error(int32_t error)
{ canopus_manager_target_record.identity_result = error; }

uint32_t test_registration_address(uint32_t which)
{
    switch (which) {
    case 0: return FW_APP_LOOKUP;
    case 1: return FW_APP_INSTALL;
#ifndef CANOPUS_MGR_BAND11
    case 2: return FW_LAUNCHER_ADD;
#else
    case 2: return FW_APP_LAUNCHER_ADD;
#endif
    case 3: return (uint32_t)(uintptr_t)&fixture_native_pages[0];
    case 4: return (uint32_t)(uintptr_t)&fixture_native_pages[1];
    case 5: return (uint32_t)fixture_source_pages;
    default: return 0u;
    }
}

uint32_t test_registration_layout(uint32_t which)
{
    switch (which) {
    case 0: return sizeof(struct firmware_app_descriptor);
    case 1: return offsetof(struct firmware_app_descriptor, package_name);
    case 2: return offsetof(struct firmware_app_descriptor, app_id);
    case 3: return offsetof(struct firmware_app_descriptor, launcher_icon_resource);
    case 4: return offsetof(struct canopus_target_page, app_id);
    case 5: return offsetof(struct canopus_target_page, page_id);
    default: return 0u;
    }
}
