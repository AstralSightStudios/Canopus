/* Native installer test ABI, independently recovered from .139 call sites.
 * Does not promote the unrelated ensemble candidates into SDK callables. */
#ifndef CANOPUS_BAND11_ABI_H
#define CANOPUS_BAND11_ABI_H
#include "canopus_target_config.h"
#include "canopus_band11_addresses.h"
#include "canopus_band11_memory.h"
#define CANOPUS_SUP_STATIC_TEST_ABI 1
/* .139 native settings list, independently traced from 0x0C5962F4 and
 * theme_apply 0x0C5048D8. See docs/NATIVE_UI_AUDIT.md in the installer. */
#define FW_LVX_LIST_TRAILING_OFFSET 0x54u
#define CANOPUS_SUP_NUTTX_OPEN FW_NUTTX_OPEN
#define CANOPUS_SUP_NUTTX_CLOSE FW_NUTTX_CLOSE
#define CANOPUS_SUP_NUTTX_READ FW_NUTTX_READ
#define CANOPUS_SUP_NUTTX_WRITE FW_NUTTX_WRITE
#define CANOPUS_SUP_REGISTER_DRIVER(path, fops, mode, priv) \
    ((void)(mode), ((int (*)(const char *, const void *, void *))(uintptr_t)B11_REGISTER_DRIVER)((path), (fops), (priv)))
#define canopus_fw_mm_memalign_default b11_alloc
#define canopus_fw_mm_free_default b11_free
/* Scratch domain for buffers the loader never executes (the input ELF and its
 * section bookkeeping). Kmem has to stay free for the resident image. */
#define canopus_fw_mm_memalign_scratch b11_temp_alloc
#define canopus_fw_mm_free_scratch b11_temp_free
#endif
