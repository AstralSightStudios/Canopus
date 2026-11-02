/* ARM fixture for the actual target descriptor/back/lifecycle implementation.
 * Registry lookup, heap and rendering are modeled; UI tree transactions use
 * the real portable runtime. Firmware finish is intercepted by Unicorn. */
#ifndef CANOPUS_MANAGER_TARGET_SOURCE
#define CANOPUS_MANAGER_TARGET_SOURCE "../../manager/target/canopus_manager_target.c"
#endif
#include CANOPUS_MANAGER_TARGET_SOURCE
#include "canopus_runtime.h"

/* The live page record. Single-page Manager entries use the target's own
 * scratch record; launcher entries get one from the modeled heap. */
static struct canopus_ui_context_v1 fixture_ui_storage;
static struct canopus_target_module_page fixture_record;
static struct canopus_target_module_page *fixture_page(void) { return module_page; }
static struct canopus_target_launcher_entry fixture_launcher_backing;
static struct canopus_supervisor_v1 fixture_supervisor;
static struct canopus_context_v1 fixture_context;
static struct canopus_ui_entry_v1 fixture_entry;
static struct canopus_target_page *fixture_descriptor;
static uint32_t fixture_mode, fixture_back_calls, fixture_close_calls;
static uint32_t fixture_released, fixture_apply_calls, fixture_bad_args;
static uint32_t fixture_routes, fixture_open_calls, fixture_resume_calls, fixture_pause_calls;
static uint32_t fixture_close_on_apply, fixture_ui_count, fixture_record_count;
static uint32_t fixture_ui_freed, fixture_records_freed;
static struct canopus_ui_context_v1 fixture_route_uis[16];
static struct canopus_target_module_page fixture_route_records[16];
static uint32_t fixture_route_stack[8], fixture_route_depth, fixture_resume_first;
static uint32_t fixture_goto_mode, fixture_pending_goto;

/* The target allocates one UI context per page on the stack while that page is
 * top; the fixture hands out the one backing store for it. */
static void *fixture_alloc(void *cookie, uint32_t size)
{
    (void)cookie;
    if (fixture_routes) {
        if (size == sizeof(fixture_ui_storage) && fixture_ui_count < 16u)
            return &fixture_route_uis[fixture_ui_count++];
        if (size == sizeof(fixture_record) && fixture_record_count < 16u)
            return &fixture_route_records[fixture_record_count++];
        fixture_bad_args++;return NULL;
    }
    if (size == sizeof(fixture_ui_storage)) return &fixture_ui_storage;
    if (size != sizeof(struct canopus_target_module_page)) {
        fixture_bad_args++;
        return NULL;
    }
    return &fixture_record;
}
static void fixture_free(void *cookie, void *ptr)
{
    (void)cookie;
    if (fixture_routes) {
        uint32_t i;
        for (i=0u;i<fixture_ui_count;i++) if (ptr == &fixture_route_uis[i]) {
            fixture_ui_freed |= 1u << i;fixture_released++;
            canopus_memset(ptr,0xcc,sizeof(fixture_ui_storage));return;
        }
        for (i=0u;i<fixture_record_count;i++) if (ptr == &fixture_route_records[i]) {
            fixture_records_freed |= 1u << i;fixture_released++;
            canopus_memset(ptr,0xcc,sizeof(fixture_record));return;
        }
        fixture_bad_args++;return;
    }
    if (ptr == &fixture_ui_storage) {
        fixture_released++;
        canopus_memset(ptr, 0xcc, sizeof(fixture_ui_storage));
        return;
    }
    if (ptr != &fixture_record) fixture_bad_args++;
    fixture_released++;
    canopus_memset(ptr, 0xcc, sizeof(fixture_record));
}
static const struct canopus_sup_platform_v1 fixture_platform = {
    .heap_alloc = fixture_alloc, .heap_release = fixture_free,
};
struct canopus_supervisor_v1 *canopus_supervisor_get(void) { return &fixture_supervisor; }
const struct canopus_context_v1 *canopus_supervisor_context(
    struct canopus_supervisor_v1 *sup, uint32_t slot)
{
    if (sup != &fixture_supervisor || slot != 3u) fixture_bad_args++;
    return &fixture_context;
}
int canopus_supervisor_ui_entry(struct canopus_supervisor_v1 *sup, uint32_t slot,
                                struct canopus_ui_entry_v1 *out)
{
    if (sup != &fixture_supervisor || slot != 3u) fixture_bad_args++;
    *out = fixture_entry; return 0;
}
int32_t canopus_supervisor_ui_pump_ensure(struct canopus_supervisor_v1 *sup)
{ (void)sup; return 0; }
int canopus_supervisor_restore_after_boot(void) { return 0; }
void canopus_supervisor_boot_ready(struct canopus_supervisor_v1 *sup) { (void)sup; }

void canopus_ui_host_attach(struct canopus_ui_host_view *view, void *root, void *page,
                            const struct canopus_ui_host_hooks *hooks, uint32_t flags)
{
    canopus_memset(view, 0, sizeof(*view));
    view->root = root; view->firmware_page = page; view->hooks = hooks;
    view->flags = flags; view->active = 1u;
}
void canopus_ui_host_bind(struct canopus_ui_host_view *view, struct canopus_ui_context_v1 *ui)
{ view->ui = ui; }
void canopus_ui_host_set_interactive(struct canopus_ui_host_view *view, int interactive)
{ view->interactive = (uint8_t)(interactive != 0); }
void canopus_ui_host_detach(struct canopus_ui_host_view *view)
{ view->active = 0u; view->interactive = 0u; }
static int32_t fixture_apply(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    if (cookie != &fixture_page()->view || snapshot->node_count != 1u) fixture_bad_args++;
    fixture_apply_calls++;
    if (fixture_close_on_apply) {
        uint32_t before=fixture_released;fixture_close_on_apply=0u;
        if (!canopus_manager_target_module_page_request_close(3u)) fixture_bad_args++;
        /* Simulate a recursive pump while an outer commit still owns ui. */
        canopus_manager_target_module_pages_poll();
        if (fixture_released != before) fixture_bad_args++;
    }
    return 0;
}
const struct canopus_ui_backend_v1 canopus_ui_host_backend = {
    .struct_size = sizeof(struct canopus_ui_backend_v1),
    .abi_major = CANOPUS_UI_ABI_MAJOR, .abi_minor = CANOPUS_UI_ABI_MINOR,
    .apply = fixture_apply,
};
static int32_t fixture_render(struct canopus_ui_context_v1 *ui, uint32_t key)
{
    struct canopus_ui_tree_v1 *tree;
    const struct canopus_ui_navigation_page_props_v1 props = {sizeof(props), "Test", 4u};
    int32_t rc = canopus_ui_tree_begin(ui, &tree);
    if (!rc) rc = canopus_ui_navigation_page(tree, key, &props);
    if (!rc) rc = canopus_ui_end(tree);
    if (!rc) rc = canopus_ui_tree_commit(tree);
    return rc;
}
static int32_t fixture_open(const struct canopus_context_v1 *ctx,
                            struct canopus_ui_context_v1 *ui, void *user)
{
    if (ctx != &fixture_context || user != &fixture_mode) fixture_bad_args++;
    fixture_open_calls++;
    return fixture_render(ui, 100u);
}
static int32_t fixture_resume(const struct canopus_context_v1 *ctx,
                              struct canopus_ui_context_v1 *ui, uint32_t route, void *user)
{
    if (ctx != &fixture_context || user != &fixture_mode) fixture_bad_args++;
    fixture_resume_calls++;return fixture_render(ui,100u+route);
}
static void fixture_pause(const struct canopus_context_v1 *ctx, uint32_t route, void *user)
{
    if (ctx != &fixture_context || user != &fixture_mode || route>=6u) fixture_bad_args++;
    fixture_pause_calls++;
}
static int32_t fixture_back(const struct canopus_context_v1 *ctx,
                            struct canopus_ui_context_v1 *ui, void *user)
{
    struct canopus_ui_tree_v1 *pending;
    if (ctx != &fixture_context || ui != fixture_page()->ui || user != &fixture_mode)
        fixture_bad_args++;
    fixture_back_calls++;
    if (fixture_mode == 2u || fixture_mode == 3u) {
        if (canopus_manager_target_module_page_close(3u) != 1) fixture_bad_args++;
        return fixture_mode == 2u ? 0 : 1;
    }
    if (fixture_mode == 0u && fixture_render(ui, 200u) != 0) fixture_bad_args++;
    /* An unfinished callback transaction must not leak to the next event. */
    if (canopus_ui_tree_begin(ui, &pending) != 0) fixture_bad_args++;
    return fixture_mode == 0u ? 0 : 1;
}
static void fixture_close(const struct canopus_context_v1 *ctx, void *user)
{
    if (ctx != &fixture_context || user != &fixture_mode) fixture_bad_args++;
    fixture_close_calls++;
}

uint32_t test_prepare(uint32_t launcher, uint32_t mode)
{
    uint32_t i;
    fixture_routes=0u;fixture_open_calls=fixture_resume_calls=fixture_pause_calls=0u;
    module_navigation.source=NULL;module_callback_depth=0u;
    canopus_memset(&fixture_supervisor, 0, sizeof(fixture_supervisor));
    canopus_memset(&fixture_record, 0, sizeof(fixture_record));
    canopus_memset(&fixture_ui_storage, 0, sizeof(fixture_ui_storage));
    canopus_memset(&fixture_entry, 0, sizeof(fixture_entry));
    module_page = NULL;
    fixture_supervisor.platform = &fixture_platform;
    fixture_mode = mode;
    fixture_back_calls = fixture_close_calls = fixture_released = 0u;
    fixture_apply_calls = fixture_bad_args = 0u;
    fixture_entry.struct_size = sizeof(fixture_entry);
    fixture_entry.on_open = fixture_open;
    fixture_entry.on_back = mode == 4u ? NULL : fixture_back;
    fixture_entry.on_close = fixture_close;
    fixture_entry.user_data = &fixture_mode;
    for (i = 0u; i < CANOPUS_TARGET_PAGE_COUNT; i++) {
        target_descriptor_init(i, "test", (uint16_t)i);
    }
    fixture_supervisor.modules[3].state = CANOPUS_STATE_ACTIVE;
    fixture_supervisor.modules[3].native_app_id = 0xccu;
    launcher_copy((char *)fixture_supervisor.modules[3].module_id,
                  sizeof(fixture_supervisor.modules[3].module_id), "page_back");
    canopus_memset(&fixture_launcher_backing, 0, sizeof(fixture_launcher_backing));
    canopus_memset(launcher_entries, 0, sizeof(launcher_entries));
    launcher_entries[0] = &fixture_launcher_backing;
    launcher_entries[0]->slot = 3u;
    launcher_entries[0]->app.app_id = fixture_supervisor.modules[3].native_app_id;
    launcher_copy(launcher_entries[0]->package, sizeof(launcher_entries[0]->package),
                  "com.canopus.module.page_back");
    launcher_entry_init(launcher_entries[0], 0u, 1u);
    fixture_descriptor = launcher ? &launcher_entries[0]->pages[0]
                                  : &manager_pages_desc[CANOPUS_TARGET_PAGE_MODULE_UI];
    manager_pending_ui_slot = 4u;
    return (uint32_t)(uintptr_t)fixture_descriptor;
}
int test_open(void)
{ return fixture_descriptor->on_create(fixture_descriptor, (void *)0x200f0000u, NULL); }
uint32_t test_callback(uint32_t which)
{
    const struct canopus_target_page *p = which == 0u ? fixture_descriptor :
        which == 1u ? &launcher_entries[0]->pages[0] : &manager_pages_desc[which - 2u];
    return (uint32_t)(uintptr_t)p->extension_callback_104;
}
uint32_t test_other_descriptor(void)
{
    return (uint32_t)(uintptr_t)(fixture_descriptor == &launcher_entries[0]->pages[0] ?
        &manager_pages_desc[CANOPUS_TARGET_PAGE_MODULE_UI] : &launcher_entries[0]->pages[0]);
}
void test_destroy(void)
{
#if defined(CANOPUS_MGR_V8_RECORDS)
    (void)fixture_descriptor->on_destroy(fixture_descriptor);
#else
    (void)fixture_descriptor->on_ui_destroy(fixture_descriptor);
#endif
}
int test_finish_destroy(void) { test_destroy(); return 0; }
void test_pause(void) { (void)fixture_descriptor->on_pause(fixture_descriptor); }
void test_inactive(void) { fixture_page()->view.active = 0u; }
void test_title_back(void) { module_page_back(&fixture_page()->view); }
uint32_t test_finish_address(void) { return FW_ACTIVITY_FINISH; }
uint32_t test_heap_address(void) { return (uint32_t)(uintptr_t)fixture_page(); }
uint32_t test_heap_size(void) { return sizeof(struct canopus_target_module_page); }
uint32_t test_state(uint32_t which)
{
    switch (which) {
    case 0: return fixture_back_calls;
    case 1: return fixture_close_calls;
    case 2: return fixture_released;
    case 3: return fixture_apply_calls;
    case 4: return fixture_bad_args;
    case 5: return module_page != NULL;
    case 6: return fixture_page() != NULL ? fixture_page()->view.interactive : 0u;
    case 7: return fixture_page() != NULL && fixture_page()->ui != NULL
                   ? fixture_page()->ui->building : 0u;
    case 8: return fixture_page() != NULL && fixture_page()->ui != NULL
                   ? fixture_page()->ui->committed.nodes[0].key : 0u;
    case 9: return fixture_open_calls;
    case 10: return fixture_resume_calls;
    case 11: return fixture_pause_calls;
    case 12: return module_navigation.source != NULL;
    default: return 0xffffffffu;
    }
}

uint32_t test_route_prepare(uint32_t resume_first)
{
    (void)test_prepare(1u,0u);fixture_routes=1u;
    fixture_ui_count=fixture_record_count=fixture_ui_freed=fixture_records_freed=0u;
    fixture_close_on_apply=fixture_goto_mode=fixture_pending_goto=0u;
    fixture_route_depth=1u;fixture_route_stack[0]=0u;
    fixture_resume_first=resume_first;
    fixture_entry.page_count=6u;fixture_entry.on_resume=fixture_resume;fixture_entry.on_pause=fixture_pause;
    launcher_entry_init(launcher_entries[0],0u,6u);
    fixture_descriptor=&launcher_entries[0]->pages[0];
    return FW_ACTIVITY_NAVIGATE;
}
int test_route_request(uint32_t route) { return canopus_manager_target_module_page_route(3u,route); }
void test_route_poll(void) { canopus_manager_target_module_pages_poll(); }
static int fixture_lifecycle(struct canopus_target_page *page, uint32_t offset)
{
    page_lifecycle_fn callback;
    canopus_memcpy(&callback, (const uint8_t *)page + offset, sizeof(callback));
    return callback != NULL ? callback(page) : 0;
}
int test_route_goto(uint32_t key)
{
    uint32_t route=key&0xffffu;
    struct canopus_target_page *page;
    if (fixture_goto_mode==2u) return -1;
    if (fixture_goto_mode==1u) {fixture_pending_goto=key;return 0;}
    if ((key>>16)!=0xccu || route>=6u || fixture_route_depth>=8u) {fixture_bad_args++;return -1;}
    page=&launcher_entries[0]->pages[route];
    if (fixture_goto_mode==3u) {
        /* Exact .043/.036 firmware slots, independent of the C field names. */
        if (module_page!=NULL) (void)fixture_lifecycle(module_page->page,92u);
        fixture_route_stack[fixture_route_depth++]=route;
        if (page->on_create(page,(void *)(uintptr_t)(0x200f0000u+route*256u),NULL)!=0) return -1;
        (void)fixture_lifecycle(page,88u);
        return fixture_lifecycle(page,80u);
    }
    if (module_page!=NULL) (void)module_page->page->on_pause(module_page->page);
    fixture_route_stack[fixture_route_depth++]=route;
    return page->on_create(page,(void *)(uintptr_t)(0x200f0000u+route*256u),NULL);
}
void test_route_suspend_resume(void)
{
    struct canopus_target_page *page=module_page->page;
    (void)page->on_pause(page);(void)page->on_resume(page);
}
void test_route_goto_mode(uint32_t mode) { fixture_goto_mode=mode; }
int test_route_complete_goto(void)
{ uint32_t key=fixture_pending_goto;fixture_goto_mode=fixture_pending_goto=0u;return test_route_goto(key); }
static void fixture_destroy_route(struct canopus_target_page *page)
{
#if defined(CANOPUS_MGR_V8_RECORDS)
    (void)fixture_lifecycle(page,92u);
#else
    (void)fixture_lifecycle(page,100u);
#endif
}
int test_route_pop(void)
{
    struct canopus_target_page *source,*destination=NULL;
    if (!fixture_route_depth) {fixture_bad_args++;return -1;}
    source=&launcher_entries[0]->pages[fixture_route_stack[--fixture_route_depth]];
    if (fixture_route_depth) destination=&launcher_entries[0]->pages[fixture_route_stack[fixture_route_depth-1u]];
    if (destination!=NULL && fixture_resume_first) (void)destination->on_resume(destination);
    fixture_destroy_route(source);
    if (destination!=NULL && !fixture_resume_first) (void)destination->on_resume(destination);
    return 0;
}
int test_route_posted_close(void)
{
    uint32_t before=fixture_released;int rc;
    fixture_close_on_apply=1u;rc=fixture_render(module_page->ui,300u);
    if (fixture_released!=before) fixture_bad_args++;
    return rc;
}
uint32_t test_route_memory(uint32_t which)
{
    switch(which) {
    case 0:return (uint32_t)(uintptr_t)fixture_route_uis;
    case 1:return sizeof(fixture_ui_storage);
    case 2:return (uint32_t)(uintptr_t)fixture_route_records;
    case 3:return sizeof(fixture_record);
    case 4:return fixture_ui_freed;
    case 5:return fixture_records_freed;
    default:return 0u;
    }
}
