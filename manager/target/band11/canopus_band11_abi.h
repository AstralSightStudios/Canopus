/* Native installer test ABI, independently recovered from .139 call sites.
 * Does not promote the unrelated ensemble candidates into SDK callables. */
#ifndef CANOPUS_BAND11_ABI_H
#define CANOPUS_BAND11_ABI_H
#include "canopus_target_config.h"
#include "canopus_band11_memory.h"
#define CANOPUS_SUP_STATIC_TEST_ABI 1
#define FW_APP_LOOKUP 0x0C6AC54Du
#define FW_APP_INSTALL 0x0C6AB351u
#define FW_APP_LAUNCHER_ADD 0x0C547B55u
#define FW_LV_OBJECT_CREATE 0x0C380687u
#define FW_LV_LABEL_CREATE 0x0C3AB301u
#define FW_LV_LABEL_SET_TEXT 0x0C3B3949u
#define FW_LV_LABEL_LONG_MODE 0x0C3B3F5Du
#define FW_LV_OBJECT_SET_SIZE 0x0C387BA9u
#define FW_LV_OBJECT_WIDTH 0x0C387B2Fu
#define FW_LV_PAGE_TITLE_CREATE 0x0C89E7DDu
/* .139 native settings list, independently traced from 0x0C5962F4 and
 * theme_apply 0x0C5048D8. See docs/NATIVE_UI_AUDIT.md in the installer. */
#define FW_LVX_LIST_ITEM_CREATE 0x0C50F4E5u
#define FW_LVX_LIST_ITEM_ADD_CONTENT 0x0C50FAD1u
#define FW_LVX_LIST_ITEM_UPDATE 0x0C50EC5Du
#define FW_LVX_STYLE_APPLY 0x0C4FBE55u
#define FW_LV_OBJECT_REMOVE_STYLES 0x0C38583Du
#define FW_LV_OBJECT_PAD_TOP 0x0C387C59u
#define FW_LV_OBJECT_PAD_BOTTOM 0x0C387C6Fu
#define FW_STYLE_MISANS_MEDIUM_28 0x200C1574u
#define FW_STYLE_MISANS_MEDIUM_24 0x200C1568u
#define FW_LVX_LIST_TRAILING_OFFSET 0x54u
#define FW_LV_EVENT_ADD 0x0C37FC1Du
#define FW_LV_ALIGN_TO 0x0C3877B1u
#define FW_LV_OBJECT_ADD_FLAG 0x0C38404Du
#define FW_LV_OBJECT_CLEAR_FLAG 0x0C3840D9u
#define FW_LV_IMAGE_CREATE 0x0C3AE1D1u
#define FW_LV_IMAGE_SET_SRC 0x0C3B2C29u
#define FW_LV_BAR_CREATE 0x0C3AD7F5u
#define FW_LV_BAR_SET_RANGE 0x0C3AD921u
#define FW_LV_BAR_SET_VALUE 0x0C3AD8B5u
#define FW_NUTTX_OPEN 0x0C342C55u
#define FW_NUTTX_CLOSE 0x0C33818Du
#define FW_NUTTX_READ 0x0C33D785u
#define FW_NUTTX_WRITE 0x0C33DC4Fu
#define FW_ACTIVITY_NAVIGATE 0x0C8ED781u
#define FW_ACTIVITY_FINISH 0x0C69B5ADu
#define CANOPUS_SUP_NUTTX_OPEN FW_NUTTX_OPEN
#define CANOPUS_SUP_NUTTX_CLOSE FW_NUTTX_CLOSE
#define CANOPUS_SUP_NUTTX_READ FW_NUTTX_READ
#define CANOPUS_SUP_NUTTX_WRITE FW_NUTTX_WRITE
#define CANOPUS_SUP_NUTTX_ERRNO_LOCATION 0x0C349539u
#define CANOPUS_SUP_NUTTX_RENAME 0x0C33D79Du
#define CANOPUS_SUP_NUTTX_UNLINK 0x0C33DBEDu
#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \
    ((void)(mode), ((int (*)(const char *, const void *, void *))(uintptr_t)0x0C914F65u)((path), (fops), (priv)))
#define canopus_fw_mm_memalign_default b11_alloc
#define canopus_fw_mm_free_default b11_free
/* Scratch domain for buffers the loader never executes (the input ELF and its
 * section bookkeeping). Kmem has to stay free for the resident image. */
#define canopus_fw_mm_memalign_scratch b11_temp_alloc
#define canopus_fw_mm_free_scratch b11_temp_free
#endif
