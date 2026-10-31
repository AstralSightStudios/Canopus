//! Authoritative C target configuration generator.
//!
//! Stable macro names are mapped to symbol names here; addresses themselves are
//! always loaded from the target pack's symbol records. No firmware address is
//! stored in this module.

use crate::model::{Symbol, TargetPack};

#[derive(Clone, Copy)]
enum Address {
    Entry,
    Callable,
    /// Address of a firmware data object (RAM global), not a call target.
    Data,
}
#[derive(Clone, Copy)]
struct Mapping {
    macro_name: &'static str,
    symbol_name: &'static str,
    address: Address,
}

const FW_UI_INPUT: &[Mapping] = &[
    Mapping {
        macro_name: "FW_UI_EVENT_GET_INDEV",
        symbol_name: "lv_event_get_indev",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_INDEV_GET_POINT",
        symbol_name: "lv_indev_get_point",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_OBJECT_GET_COORDS",
        symbol_name: "lv_obj_get_coords",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_OBJECT_GET_SCROLL_Y",
        symbol_name: "lv_obj_get_scroll_y",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_OBJECT_GET_SCROLL_X",
        symbol_name: "lv_obj_get_scroll_x",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_OBJECT_SCROLL_TO",
        symbol_name: "lv_obj_scroll_to",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_TICK_GET",
        symbol_name: "lv_tick_get",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_UPDATE_LAYOUT",
        symbol_name: "lv_obj_update_layout",
        address: Address::Callable,
    },
];

const FW_UI_IMAGE_FIT: &[Mapping] = &[
    Mapping {
        macro_name: "FW_UI_IMAGE_SCALE",
        symbol_name: "lvx_image_set_scale",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_IMAGE_INFO",
        symbol_name: "lv_image_decoder_get_info",
        address: Address::Callable,
    },
];

const FW_UI_FONT: &[Mapping] = &[
    Mapping {
        macro_name: "FW_UI_FONT_CREATE",
        symbol_name: "native_font_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_FONT_RELEASE",
        symbol_name: "native_font_release",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_FONT_WAIT",
        symbol_name: "native_font_wait",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_FONT_MANAGER_SLOT",
        symbol_name: "font_reload_uikit_slot",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_UI_FONT_DRAW_UNITS",
        symbol_name: "font_reload_draw_unit_slot",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_UI_FONT_DISPLAY_LIST",
        symbol_name: "font_reload_display_list",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_UI_FONT_VG_DISPATCH",
        symbol_name: "font_reload_vg_dispatch",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_FONT_SW_DISPATCH",
        symbol_name: "font_reload_sw_dispatch",
        address: Address::Callable,
    },
];

const FW_10: &[Mapping] = &[
    Mapping {
        macro_name: "FW_VERSION_ADDRESS",
        symbol_name: "firmware_version_string",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_BUILD_ADDRESS",
        symbol_name: "firmware_build_string",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_APP_LOOKUP",
        symbol_name: "app_lookup",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_APP_INSTALL",
        symbol_name: "app_install",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LAUNCHER_ADD",
        symbol_name: "app_launcher_add",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LIST_ROW_CREATE",
        symbol_name: "lvx_list_row_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LIST_ROW_UPDATE",
        symbol_name: "lvx_list_item_update",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LIST_ROW_TRAILING",
        symbol_name: "lvx_list_row_trailing",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LABEL_CREATE",
        symbol_name: "lvx_label_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LABEL_SET_TEXT",
        symbol_name: "lvx_label_set_text",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_CONTENT_CREATE",
        symbol_name: "lvx_page_content_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_SET_SIZE",
        symbol_name: "lvx_object_set_size",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_ALIGN",
        symbol_name: "lvx_object_align",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_PAGE_TITLE_CREATE",
        symbol_name: "lvx_page_title_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_UI_STYLE_SET",
        symbol_name: "lv_obj_set_local_style_prop",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_STYLE_APPLY",
        symbol_name: "lvx_style_apply",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_STYLE_MISANS_DEMIBOLD_32",
        symbol_name: "style_misans_demibold_32",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_STYLE_MISANS_MEDIUM_24",
        symbol_name: "style_misans_medium_24",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_STYLE_MISANS_MEDIUM_28",
        symbol_name: "style_misans_medium_28",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_LVX_EVENT_ADD",
        symbol_name: "lv_obj_add_event_cb",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_EVENT_GET_USER_DATA",
        symbol_name: "lv_event_get_user_data",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_EVENT_GET_CODE",
        symbol_name: "lv_event_get_code",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_ADD_FLAG",
        symbol_name: "lvx_object_add_flag",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_CLEAR_FLAG",
        symbol_name: "lv_obj_clear_flag",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_ALIGN_TO",
        symbol_name: "lv_obj_align_to",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_CONTENT_PAD_BOTTOM",
        symbol_name: "lvx_content_pad_bottom",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_IMAGE_CREATE",
        symbol_name: "lv_image_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_IMAGE_SET_SRC",
        symbol_name: "lv_image_set_src",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_BAR_CREATE",
        symbol_name: "lv_bar_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_BAR_SET_RANGE",
        symbol_name: "lv_bar_set_range",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_BAR_SET_VALUE",
        symbol_name: "lv_bar_set_value",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_OPEN",
        symbol_name: "open",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_CLOSE",
        symbol_name: "close",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_READ",
        symbol_name: "read",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_WRITE",
        symbol_name: "write",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NOTIFICATION_INSERT",
        symbol_name: "lvx_notification_insert_message",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_ACTIVITY_NAVIGATE",
        symbol_name: "page_goto",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_ACTIVITY_FINISH",
        symbol_name: "page_finish",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_MSGBOX_CREATE",
        symbol_name: "lvx_msgbox_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_MSGBOX_SET_CONTENT",
        symbol_name: "lvx_msgbox_set_content",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_DELETE",
        symbol_name: "lvx_object_delete",
        address: Address::Callable,
    },
];

/// LVGL v8 text font. Optional: only the 3.1.28 image has both records
/// recovered, and the v8 Manager backend guards on the macro, so a target
/// without them still builds a complete Supervisor.
const V8_TEXT_FONT: &[Mapping] = &[
    Mapping {
        macro_name: "FW_PAGE_LOOKUP",
        symbol_name: "page_lookup",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJ_GET_DISPLAY",
        symbol_name: "lv_obj_get_display",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_SCREEN_STATE_GET",
        symbol_name: "screen_state_get",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_TIMER_CREATE",
        symbol_name: "lv_timer_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_TIMER_DEL",
        symbol_name: "lv_timer_del",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_FONT_CREATE",
        symbol_name: "lvx_font_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_SET_TEXT_FONT",
        symbol_name: "lv_obj_set_text_font",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_SET_LOCAL_STYLE_PROP",
        symbol_name: "lv_obj_set_local_style_prop",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_PAGE_CONTENT_CREATE",
        symbol_name: "lvx_page_content_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_SCROLL_TO_Y",
        symbol_name: "lv_obj_scroll_to_y",
        address: Address::Callable,
    },
];

/// Optional per-target extras: emitted only when the record exists, and never
/// part of the platform-completeness contract (a target may reach the launcher
/// by other means, e.g. 3.1.32's page-manager list).
const FW_LAUNCHER_OPTIONAL: &[Mapping] = &[Mapping {
    macro_name: "FW_LAUNCHER_APP_INSTANCE",
    symbol_name: "launcher_app_instance",
    address: Address::Data,
}];

const FW_9: &[Mapping] = &[
    Mapping {
        macro_name: "FW_VERSION_ADDRESS",
        symbol_name: "firmware_version_string",
        address: Address::Entry,
    },
    Mapping {
        macro_name: "FW_APP_LOOKUP",
        symbol_name: "app_lookup",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_APP_INSTALL",
        symbol_name: "app_install",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LAUNCHER_LOAD_APP_INFO",
        symbol_name: "launcher_load_app_info",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LAUNCHER_PAGE_INSERT_ICON",
        symbol_name: "launcher_page_insert_icon",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LAUNCHER_RESET_ORDER_INFO",
        symbol_name: "launcher_reset_order_info",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_CREATE",
        symbol_name: "lv_obj_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_LABEL_CREATE",
        symbol_name: "lv_label_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_LABEL_SET_TEXT",
        symbol_name: "lv_label_set_text",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_SET_SIZE",
        symbol_name: "lv_obj_set_size",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_PAGE_TITLE_CREATE",
        symbol_name: "lvx_page_title_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_EVENT_ADD",
        symbol_name: "lv_obj_add_event_cb",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_ALIGN_TO",
        symbol_name: "lv_obj_align_to",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_ADD_FLAG",
        symbol_name: "lv_obj_add_flag",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_OBJECT_CLEAR_FLAG",
        symbol_name: "lv_obj_clear_flag",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_IMAGE_CREATE",
        symbol_name: "lv_img_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_IMAGE_SET_SRC",
        symbol_name: "lv_img_set_src",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_BAR_CREATE",
        symbol_name: "lv_bar_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_BAR_SET_RANGE",
        symbol_name: "lv_bar_set_range",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LV_BAR_SET_VALUE",
        symbol_name: "lv_bar_set_value",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_OPEN",
        symbol_name: "open",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_CLOSE",
        symbol_name: "close",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_READ",
        symbol_name: "read",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_NUTTX_WRITE",
        symbol_name: "write",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_ACTIVITY_NAVIGATE",
        symbol_name: "page_goto",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_ACTIVITY_FINISH",
        symbol_name: "page_finish",
        address: Address::Callable,
    },
];
const SUP_10: &[Mapping] = &[
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_OPEN",
        symbol_name: "open",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_CLOSE",
        symbol_name: "close",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_READ",
        symbol_name: "read",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_ERRNO_LOCATION",
        symbol_name: "errno_location",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_WRITE",
        symbol_name: "write",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_RENAME",
        symbol_name: "rename",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_UNLINK",
        symbol_name: "unlink",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_RMDIR",
        symbol_name: "rmdir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_LSEEK",
        symbol_name: "lseek",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_INSMOD",
        symbol_name: "insmod",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_RMMOD",
        symbol_name: "rmmod",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_MODHANDLE",
        symbol_name: "modhandle",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_WATCHFACE_DELETE",
        symbol_name: "watchface_manager_delete_watchface",
        address: Address::Callable,
    },
    // Optional: directory iteration for the phone-installed package scan.
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_OPENDIR",
        symbol_name: "opendir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_READDIR",
        symbol_name: "readdir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_CLOSEDIR",
        symbol_name: "closedir",
        address: Address::Callable,
    },
    // Optional: back the MEM service (Supervisor-owned allocations only).
    Mapping {
        macro_name: "CANOPUS_SUP_HEAP_MALLOC",
        symbol_name: "heap_malloc",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_HEAP_FREE",
        symbol_name: "heap_free",
        address: Address::Callable,
    },
    // Optional: the UI-owner heartbeat for service timers.
    Mapping {
        macro_name: "CANOPUS_SUP_LV_TIMER_CREATE",
        symbol_name: "lv_timer_create",
        address: Address::Callable,
    },
    // Optional: backs the TIME service (monotonic milliseconds).
    Mapping {
        macro_name: "CANOPUS_SUP_CLOCK_GETTIME",
        symbol_name: "clock_gettime",
        address: Address::Callable,
    },
    // Optional: only packs that recovered the post-delete watchface refresh
    // emit this macro, so Supervisor code must guard on it.
    Mapping {
        macro_name: "CANOPUS_SUP_WATCHFACE_RESET",
        symbol_name: "watchface_reset_watchface",
        address: Address::Callable,
    },
    // Optional: stock hardware services behind the Supervisor platform hooks
    // (battery, wake locks, screen/brightness, vibration, properties, Bluetooth
    // adapter state). Each is guarded in Supervisor code; a missing symbol leaves
    // the matching service group reporting UNSUPPORTED.
    Mapping {
        macro_name: "CANOPUS_SUP_PM_BATTERY_READY",
        symbol_name: "power_battery_ready",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_PM_BATTERY_CHARGING",
        symbol_name: "power_battery_charging",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_PM_BATTERY_LEVEL",
        symbol_name: "power_battery_level",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_WAKELOCK_CREATE",
        symbol_name: "wakelock_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_WAKELOCK_ACQUIRE",
        symbol_name: "wakelock_acquire",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_WAKELOCK_RELEASE",
        symbol_name: "wakelock_release",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_GET_STATE",
        symbol_name: "screen_get_state",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_TURN_ON",
        symbol_name: "screen_turn_on",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_CREATE",
        symbol_name: "screen_session_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_START",
        symbol_name: "screen_session_start",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_STOP",
        symbol_name: "screen_session_stop",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_SET_AUTO_BRIGHTNESS",
        symbol_name: "screen_session_set_auto_brightness",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_SET_BRIGHTNESS_VALUE",
        symbol_name: "screen_session_set_brightness_value",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SCREEN_SESSION_RESTORE_DEFAULT",
        symbol_name: "screen_session_restore_default",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_VIBRATOR_HAL_TRANSACT",
        symbol_name: "vibrator_hal_transact",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_PROPERTY_GET",
        symbol_name: "property_get",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_PROPERTY_GET_INT32",
        symbol_name: "property_get_int32",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_BT_ADAPTER_GET_INSTANCE",
        symbol_name: "bt_adapter_get_instance",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_BT_ADAPTER_GET_STATE",
        symbol_name: "bt_adapter_get_state",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_MKDIR",
        symbol_name: "mkdir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_FSYNC",
        symbol_name: "fsync",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_STAT",
        symbol_name: "stat",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SYSTEM_GET_STATE",
        symbol_name: "system_get_state",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_SYSTEM_STATE",
        symbol_name: "system_state",
        address: Address::Entry,
    },
];
const SUP_9: &[Mapping] = &[
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_OPEN",
        symbol_name: "open",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_CLOSE",
        symbol_name: "close",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_READ",
        symbol_name: "read",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_WRITE",
        symbol_name: "write",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_ERRNO_LOCATION",
        symbol_name: "errno_location",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_RENAME",
        symbol_name: "rename",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_UNLINK",
        symbol_name: "unlink",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_WATCHFACE_DELETE",
        symbol_name: "watchface_manager_delete_watchface",
        address: Address::Callable,
    },
];

// Optional: monotonic clock and directory iteration for custom staged targets.
// These are not part of the core platform completeness gate: a target without
// them still builds, but its Time/package-scan services report UNSUPPORTED.
const SUP_9_DIRENT: &[Mapping] = &[
    Mapping {
        macro_name: "CANOPUS_SUP_CLOCK_GETTIME",
        symbol_name: "clock_gettime",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_LV_TIMER_CREATE",
        symbol_name: "lv_timer_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_OPENDIR",
        symbol_name: "opendir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_READDIR",
        symbol_name: "readdir",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "CANOPUS_SUP_NUTTX_CLOSEDIR",
        symbol_name: "closedir",
        address: Address::Callable,
    },
];

pub struct TargetConfigGen<'a> {
    pub pack: &'a TargetPack,
    pub symbols: &'a [Symbol],
}

impl<'a> TargetConfigGen<'a> {
    fn symbol(&self, name: &str) -> Option<&'a Symbol> {
        self.symbols.iter().find(|s| s.name == name)
    }
    fn address(&self, map: Mapping) -> Option<u64> {
        let s = self.symbol(map.symbol_name)?;
        let raw = match map.address {
            Address::Entry | Address::Data => s.entry_address.as_deref(),
            Address::Callable => s.callable_address.as_deref(),
        }?;
        parse_hex_address(raw)
    }
    fn emit(&self, out: &mut String, maps: &[Mapping], aliases: bool) {
        for map in maps {
            if let Some(address) = self.address(*map) {
                if aliases
                    && matches!(
                        map.macro_name,
                        "CANOPUS_SUP_NUTTX_OPEN"
                            | "CANOPUS_SUP_NUTTX_CLOSE"
                            | "CANOPUS_SUP_NUTTX_READ"
                            | "CANOPUS_SUP_NUTTX_WRITE"
                    )
                {
                    let fw = map.macro_name.strip_prefix("CANOPUS_SUP_").unwrap();
                    out.push_str(&format!("#define {} FW_{}\n", map.macro_name, fw));
                } else {
                    out.push_str(&format!(
                        "#define {} UINT32_C(0x{address:08X})\n",
                        map.macro_name
                    ));
                }
            }
        }
    }
    fn custom_loader(&self) -> bool {
        self.pack.loader != "nuttx-modlib-elf32-rel"
    }
    fn static_test(&self) -> bool {
        self.pack.static_test_abi == Some(true)
    }
    /// Production codegen needs an explicit approval. A static-test pack may
    /// additionally expose restricted STATIC_RECOVERED records as address
    /// constants for loader/ABI bring-up; that is still not an approval and
    /// the generated header carries `CANOPUS_SUP_STATIC_TEST_ABI`.
    fn codegen_available(&self, symbol: &Symbol) -> bool {
        let production = symbol.policy != "restricted"
            && symbol.policy != "forbidden"
            && symbol.approved_for_codegen();
        let static_test = self.static_test()
            && symbol.status == "STATIC_RECOVERED"
            && symbol.policy == "restricted";
        production || static_test
    }
    fn veneer_callable_available(&self, name: &str) -> bool {
        self.symbol(name).is_some_and(|symbol| {
            symbol.callable_address.is_some() && self.codegen_available(symbol)
        })
    }
    fn mapping_available(&self, map: Mapping) -> bool {
        let Some(symbol) = self.symbol(map.symbol_name) else {
            return false;
        };
        self.address(map).is_some() && self.codegen_available(symbol)
    }
    fn platform_complete(&self, custom: bool) -> bool {
        // Stock modlib targets retain their established production contract. The
        // explicit completeness gate below covers custom-loader targets, whose
        // Supervisor embeds every firmware callable and allocator/MPU veneer.
        if !custom {
            return true;
        }

        let mapped = FW_9
            .iter()
            .chain(SUP_9.iter())
            .all(|mapping| self.mapping_available(*mapping));
        let veneer = [
            "register_driver",
            "unregister_driver",
            "mm_memalign_default",
            "mm_free_default",
            "mpu_region_allocate",
            "mpu_region_configure",
            "mpu_region_release",
        ];
        mapped
            && veneer
                .into_iter()
                .all(|name| self.veneer_callable_available(name))
    }

    pub fn generate(&self) -> String {
        let custom = self.custom_loader();
        let complete = self.platform_complete(custom);
        let mut out = String::new();
        out.push_str("#ifndef CANOPUS_TARGET_CONFIG_H\n#define CANOPUS_TARGET_CONFIG_H\n\n#include <stdint.h>\n\n");
        out.push_str(&format!("#define CANOPUS_TARGET_ID \"{}\"\n#define CANOPUS_TARGET_FIRMWARE_VERSION \"{}\"\n#define CANOPUS_TARGET_FIRMWARE_BUILD \\\n    \"{}\"\n#define CANOPUS_SUP_PLATFORM_COMPLETE {}\n", self.pack.target_id, self.pack.firmware_version, self.pack.firmware_build, u8::from(complete)));
        if custom {
            out.push_str("#define CANOPUS_SUP_CUSTOM_LOADER 1\n");
        }
        if self.static_test() {
            out.push_str("/* Static bring-up ABI only: restricted records are addresses,\n * not approvals. Do not ship this build as a production artifact. */\n#define CANOPUS_SUP_STATIC_TEST_ABI 1\n");
        }
        out.push('\n');
        // A zero completeness flag must not accompany executable candidate
        // addresses: consumers other than the Supervisor build script can
        // include this header. Incomplete custom targets expose identity only.
        if complete {
            self.emit(&mut out, if custom { FW_9 } else { FW_10 }, false);
            if custom {
                self.emit(&mut out, FW_LAUNCHER_OPTIONAL, false);
            }
            self.emit(&mut out, FW_UI_INPUT, false);
            self.emit(&mut out, V8_TEXT_FONT, false);
            if self.pack.target_id == "xiaomi-band-10-pro-3.101.043" {
                // Exact private image ABI: EVID-UI-1043-IMAGE-FIT-001.
                self.emit(&mut out, FW_UI_IMAGE_FIT, false);
                self.emit(&mut out, FW_UI_FONT, false);
                out.push_str("#define FW_UI_FONT_EVENT_DELETE 36u\n");
                // Exact private properties: EVID-UI-1043-LAYOUT-INPUT-001.
                out.push_str("#define FW_UI_COORD_TYPE int32_t\n#define FW_UI_EVENT_PRESSED 1u\n#define FW_UI_EVENT_PRESSING 2u\n#define FW_UI_EVENT_PRESS_LOST 3u\n#define FW_UI_EVENT_LONG_PRESSED 5u\n#define FW_UI_EVENT_RELEASED 8u\n#define FW_UI_EVENT_SCROLL 12u\n#define FW_UI_STYLE_RADIUS 12u\n#define FW_UI_STYLE_CLIP_CORNER 45u\n#define FW_UI_IMAGE_CREATE_THEME_ARG 1\n");
                out.push_str("/* Exact-target opacity: EVID-UI-1043-OPACITY-001; static only. */\n#define FW_UI_STYLE_OPA 95u\n");
            } else if self.pack.target_id == "xiaomi-band-10-pro-3.101.036" {
                out.push_str("/* Exact-target opacity and SRAM dispatcher: EVID-UI-1036-OPACITY-001; static only. */\n#define FW_UI_STYLE_OPA 95u\n");
            }
            out.push('\n');
            self.emit(&mut out, if custom { SUP_9 } else { SUP_10 }, custom);
            if custom {
                self.emit(&mut out, SUP_9_DIRENT, custom);
            }
        } else {
            out.push_str("/* Platform ABI incomplete: firmware call macros are withheld. */\n");
        }
        out.push_str("#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID\n");
        if complete && custom {
            if self.pack.target_id == "redmi-watch-6-3.100.043" {
                // p65's NuttX keeps the four-argument register_driver: the
                // mode reaches inode_reserve and the private data is stored
                // at +24, so dropping either argument would corrupt the
                // inode. EVID-STATIC-P65-002.
                out.push_str("#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \\\n    canopus_fw_register_driver((path), (fops), (mode), (priv))\n");
            } else {
                out.push_str("#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \\\n    ((void)(mode), canopus_fw_register_driver((path), (fops), (priv)))\n");
            }
        } else if complete {
            out.push_str("#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \\\n    canopus_fw_register_driver((path), (fops), (mode), (priv))\n");
        }
        if complete {
            out.push_str(
                "#define CANOPUS_SUP_UNREGISTER_DRIVER(path) canopus_fw_unregister_driver(path)\n",
            );
        }
        out.push_str("#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \\\n");
        for i in 0..32 {
            if i == 0 {
                out.push_str("    { ");
            } else if i % 8 == 0 {
                out.push_str("      ");
            }
            let pair = &self.pack.firmware_sha256[i * 2..i * 2 + 2];
            out.push_str(&format!("0x{}", pair.to_ascii_lowercase()));
            if i == 31 {
                out.push_str(" }\n");
            } else if i % 8 == 7 {
                out.push_str(", \\\n");
            } else {
                out.push_str(", ");
            }
        }
        out.push_str("\n#endif\n");
        out
    }
}

fn parse_hex_address(value: &str) -> Option<u64> {
    u64::from_str_radix(
        value
            .trim()
            .trim_start_matches("0x")
            .trim_start_matches("0X"),
        16,
    )
    .ok()
}

#[cfg(test)]
mod tests {
    use super::parse_hex_address;
    #[test]
    fn parses_symbol_addresses() {
        assert_eq!(parse_hex_address("0x0C0C0810"), Some(0x0C0C0810));
        assert_eq!(parse_hex_address("nope"), None);
    }
}
