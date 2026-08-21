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
}
#[derive(Clone, Copy)]
struct Mapping {
    macro_name: &'static str,
    symbol_name: &'static str,
    address: Address,
}

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
        macro_name: "FW_LVX_SET_HIDDEN",
        symbol_name: "lv_obj_set_hidden",
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

const FW_9: &[Mapping] = &[
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
        symbol_name: "lvx_list_row_update",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_LIST_ROW_SET_TRAILING",
        symbol_name: "lvx_list_row_set_trailing",
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
        macro_name: "FW_LVX_EVENT_ADD",
        symbol_name: "lvx_event_add",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_ALIGN_TO",
        symbol_name: "lvx_align_to",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_OBJECT_ADD_FLAG",
        symbol_name: "lvx_object_add_flag",
        address: Address::Callable,
    },
    // The record may be restricted, but the config surface must retain this
    // explicit name mapping; emission still requires its target-pack record.
    Mapping {
        macro_name: "FW_LVX_OBJECT_CLEAR_FLAG",
        symbol_name: "lvx_object_clear_flag",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_IMAGE_CREATE",
        symbol_name: "lv_img_create",
        address: Address::Callable,
    },
    Mapping {
        macro_name: "FW_LVX_IMAGE_SET_SRC",
        symbol_name: "lv_img_set_src",
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
        symbol_name: "manager_delete_watchface",
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
            Address::Entry => s.entry_address.as_deref(),
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

    pub fn generate(&self) -> String {
        let custom = self.custom_loader();
        let mut out = String::new();
        out.push_str("#ifndef CANOPUS_TARGET_CONFIG_H\n#define CANOPUS_TARGET_CONFIG_H\n\n#include <stdint.h>\n\n");
        out.push_str(&format!("#define CANOPUS_TARGET_ID \"{}\"\n#define CANOPUS_TARGET_FIRMWARE_VERSION \"{}\"\n#define CANOPUS_TARGET_FIRMWARE_BUILD \\\n    \"{}\"\n#define CANOPUS_SUP_PLATFORM_COMPLETE 1\n", self.pack.target_id, self.pack.firmware_version, self.pack.firmware_build));
        if custom {
            out.push_str("#define CANOPUS_SUP_CUSTOM_LOADER 1\n");
        }
        out.push('\n');
        self.emit(&mut out, if custom { FW_9 } else { FW_10 }, false);
        out.push('\n');
        self.emit(&mut out, if custom { SUP_9 } else { SUP_10 }, custom);
        out.push_str("#define CANOPUS_SUP_TARGET_ID CANOPUS_TARGET_ID\n");
        if custom {
            out.push_str("#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \\\n    ((void)(mode), canopus_fw_register_driver((path), (fops), (priv)))\n");
        } else {
            out.push_str("#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \\\n    canopus_fw_register_driver((path), (fops), (mode), (priv))\n");
        }
        out.push_str("#define CANOPUS_SUP_UNREGISTER_DRIVER(path) canopus_fw_unregister_driver(path)\n#define CANOPUS_SUP_FIRMWARE_SHA256_BYTES \\\n");
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
