/* Native installer test ABI, independently recovered from .139 call sites.
 * Does not promote the unrelated ensemble candidates into SDK callables. */
#ifndef CANOPUS_BAND11_ABI_H
#define CANOPUS_BAND11_ABI_H
#include "canopus_target_config.h"
#include "canopus_band11_addresses.h"
#ifndef CANOPUS_MGR_REDMI_WATCH
/* REDMI Watch 6 supplies its own allocator/MPU/registration ABI through the
 * generated target config and the native bindings; the Band 11 bootstrap
 * helpers (and the B11_* slots they need) do not apply there. */
#include "canopus_band11_memory.h"
#endif
#define CANOPUS_SUP_STATIC_TEST_ABI 1
/* .139 native settings list, independently traced from 0x0C5962F4 and
 * theme_apply 0x0C5048D8. See docs/NATIVE_UI_AUDIT.md in the installer. */
#ifndef CANOPUS_MGR_REDMI_WATCH
#define FW_LVX_LIST_TRAILING_OFFSET 0x54u
/* The reviewed .139/.155/.179 title constructor binds its click callback to
 * the icon and text children. See .155 sub_C89E7CC, fields +52 and +56. */
#define FW_LVX_TITLE_ICON_OFFSET 0x34u
#define FW_LVX_TITLE_LABEL_OFFSET 0x38u
#endif
#define CANOPUS_SUP_NUTTX_OPEN FW_NUTTX_OPEN
#define CANOPUS_SUP_NUTTX_CLOSE FW_NUTTX_CLOSE
#define CANOPUS_SUP_NUTTX_READ FW_NUTTX_READ
#define CANOPUS_SUP_NUTTX_WRITE FW_NUTTX_WRITE
#define CANOPUS_SUP_NUTTX_LSEEK FW_NUTTX_LSEEK
#ifndef CANOPUS_MGR_REDMI_WATCH
#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \
    ((void)(mode), ((int (*)(const char *, const void *, void *))(uintptr_t)B11_REGISTER_DRIVER)((path), (fops), (priv)))
#define canopus_fw_mm_memalign_default b11_alloc
#define canopus_fw_mm_free_default b11_free
/* Scratch domain for buffers the loader never executes (the input ELF and its
 * section bookkeeping). Kmem has to stay free for the resident image. */
#define canopus_fw_mm_memalign_scratch b11_temp_alloc
#define canopus_fw_mm_free_scratch b11_temp_free
#endif
#endif
