#ifndef CANOPUS_MANAGER_TARGET_H
#define CANOPUS_MANAGER_TARGET_H

#include <stdint.h>

#define CANOPUS_MANAGER_TARGET_APP_ID UINT16_C(0x00CA)
#define CANOPUS_MANAGER_TARGET_PAGE_ID UINT16_C(0)
#define CANOPUS_MANAGER_TARGET_MAGIC UINT32_C(0x434E5431) /* "CNT1" */
#define CANOPUS_MANAGER_TARGET_BUILD_ID UINT32_C(0x26081202)

struct canopus_manager_target_record {
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
    uint32_t build_id;
    uint32_t click_count;
    uint32_t clicked_row;
    uint32_t clicked_generation;
    uint32_t clicked_key;
    uint32_t clicked_event;
    uint32_t selected_before;
    uint32_t selected_after;
    uint32_t detail_selected;
};

extern volatile struct canopus_manager_target_record canopus_manager_target_record;

void canopus_manager_target_render_diagnostics(uint8_t out[36]);
int canopus_manager_native_install(void);
int canopus_manager_native_open(void);
/* Publishes the launcher icon while the launcher page is alive; 0 when it is
 * published for the current launcher page, negative otherwise. */
int canopus_manager_native_publish_launcher(void);
/* Finishes the page a module opened through ctx->ui when it belongs to `slot`
 * (the module closed it or was unloaded). Returns 1 when a page was closed. */
int canopus_manager_target_module_page_close(uint32_t slot);
/* Moves a multi-page module UI entry to `route` through the firmware page
 * stack: forward routes push (pausing the source), backward routes finish, both
 * with the stock transition. Returns 0 on success or a service error. */
int canopus_manager_target_module_page_route(uint32_t slot, uint32_t route);
/* Queue one application with managed UI or exact-target native pages.
 * Registration advances one firmware mutation per UI event turn. */
struct canopus_app_entry_v1;
int canopus_manager_target_app_publish(uint32_t slot,
                                       const struct canopus_app_entry_v1 *entry);
int canopus_manager_target_launcher_step(void);
int canopus_manager_target_native_app_id_in_use(uint16_t app_id);
int canopus_manager_native_notify_module_installed(void);
/* Post a notification attributed to `source` with a caller-supplied title and
 * body (copied by the firmware synchronously). `icon` is the absolute path of
 * the small/large icon image, or NULL for the Canopus icon. Returns 0 on
 * success, negative on failure. */
int canopus_manager_native_notify(const char *source, const char *title, const char *body,
                                  const char *icon);

#endif
