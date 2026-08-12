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
int canopus_manager_native_notify_module_installed(void);

#endif
