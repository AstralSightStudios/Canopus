/*
 * native_demo.c — Canopus Native Application Demo module.
 *
 * Demonstrates:
 *   - Supervisor-owned native application and launcher registration
 *   - UI page creation with custom widgets (PageTitle, Content, Labels, Rows, Actions)
 *   - Microkernel Service Objects: ctx->system, ctx->kv, ctx->fs, ctx->log, ctx->haptics
 *   - Interactive touch event handling with haptic feedback
 */

#include "canopus_module_entry.h"
#include "canopus_memory.h"

#define DEMO_MODULE_ID "native_demo"

#define CANOPUS_TARGET_ALIGN_TOP_MID 2u
#define CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID 14u
#define CANOPUS_TARGET_CONTENT_TOP_OFFSET 56
#define CANOPUS_TARGET_CONTENT_WIDTH 336
#define CANOPUS_TARGET_CONTENT_HEIGHT 424
#define CANOPUS_TARGET_ROW_GAP 8
#define CANOPUS_TARGET_EVENT_CLICKED 7u
#define CANOPUS_TARGET_TRAILING_FORWARD 3u

static const struct canopus_context_v1 *s_ctx;
static uint32_t s_run_count;
static uint32_t s_batt_level;
static uint32_t s_click_count;

static const char s_launcher_icon[] = "/resource/app/launcher/compass.bin";
static const char s_display_name[] = "Native Demo";

static uint16_t s_demo_app_id;
static struct firmware_page_descriptor s_demo_page;

static void format_u32(char *buf, uint32_t val)
{
    char tmp[16];
    int i = 0, j = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = (char)('0' + (val % 10));
        val /= 10;
    }
    while (i > 0) {
        buf[j++] = tmp[--i];
    }
    buf[j] = '\0';
}

static void format_hex(char *buf, uintptr_t val)
{
    const char hex_chars[] = "0123456789abcdef";
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        buf[2 + (7 - i)] = hex_chars[(val >> (i * 4)) & 0xf];
    }
    buf[10] = '\0';
}

static void on_vibrate_click(void *event)
{
    (void)event;
    s_click_count++;
    if (s_ctx != 0) {
        if (s_ctx->haptics != 0 && s_ctx->haptics->vibrate_preset != 0) {
            s_ctx->haptics->vibrate_preset(s_ctx, CANOPUS_HAPTIC_CLICK);
        }
        if (s_ctx->kv != 0 && s_ctx->kv->set_int != 0) {
            s_ctx->kv->set_int(s_ctx, "clicks", (int32_t)s_click_count);
        }
        if (s_ctx->log != 0 && s_ctx->log->info != 0) {
            s_ctx->log->info(s_ctx, "native_demo: vibrate action triggered");
        }
    }
}

static int demo_on_signal(struct firmware_page_descriptor *page, uint32_t event, void *payload)
{
    (void)page;
    (void)event;
    (void)payload;
    return 0;
}

static int demo_on_create(struct firmware_page_descriptor *page, void *root, void *start_data)
{
    void *title;
    void *content;
    void *header;
    void *row_dev;
    void *row_runs;
    void *row_batt;
    void *row_act;
    void *prev;
    char runs_buf[16];
    char batt_buf[16];
    char batt_display[24];
    char probe_buf[256];
    char *p = probe_buf;
    char hex[16];
    int bi = 0;

    (void)page;
    (void)start_data;

    /* 1. Page Title Header */
    title = canopus_fw_lvx_page_title_create(root, "Canopus Demo", 0u, 0, 0);

    /* 2. Content Scrolling Area */
    content = canopus_fw_lvx_page_content_create(root);
    if (content != 0) {
        canopus_fw_lvx_object_set_size(content, CANOPUS_TARGET_CONTENT_WIDTH, CANOPUS_TARGET_CONTENT_HEIGHT);
        canopus_fw_lvx_object_align(content, CANOPUS_TARGET_ALIGN_TOP_MID, 0, CANOPUS_TARGET_CONTENT_TOP_OFFSET);
    }

    /* 3. Subtitle Header Label */
    header = content ? canopus_fw_lvx_label_create(content) : 0;
    if (header != 0) {
        canopus_fw_lvx_label_set_text(header, "Native Extension App");
        canopus_fw_lv_obj_align_to(header, content, CANOPUS_TARGET_ALIGN_TOP_MID, 0, 4);
    }
    prev = header ? header : content;

    /* 4. Row 1: Device Information */
    row_dev = content ? canopus_fw_lvx_list_row_create(content, "Device", "Xiaomi Band 10 Pro", 0u) : 0;
    if (row_dev != 0) {
        if (prev) canopus_fw_lv_obj_align_to(row_dev, prev, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, 10);
        prev = row_dev;
    }

    /* 5. Row 2: Activation Run Counter */
    format_u32(runs_buf, s_run_count);
    row_runs = content ? canopus_fw_lvx_list_row_create(content, "Run Count", runs_buf, 0u) : 0;
    if (row_runs != 0) {
        if (prev) canopus_fw_lv_obj_align_to(row_runs, prev, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, CANOPUS_TARGET_ROW_GAP);
        prev = row_runs;
    }

    /* 6. Row 3: Battery Level */
    format_u32(batt_buf, s_batt_level ? s_batt_level : 85u);
    for (int k = 0; batt_buf[k] != '\0'; k++) {
        batt_display[bi++] = batt_buf[k];
    }
    batt_display[bi++] = '%';
    batt_display[bi] = '\0';
    row_batt = content ? canopus_fw_lvx_list_row_create(content, "Battery", batt_display, 0u) : 0;
    if (row_batt != 0) {
        if (prev) canopus_fw_lv_obj_align_to(row_batt, prev, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, CANOPUS_TARGET_ROW_GAP);
        prev = row_batt;
    }

    /* 7. Row 4: Interactive Haptic Feedback Action Row */
    row_act = content ? canopus_fw_lvx_list_row_create(content, "Haptic Feedback", "Tap to test vibrate", CANOPUS_TARGET_TRAILING_FORWARD) : 0;
    if (row_act != 0) {
        if (prev) canopus_fw_lv_obj_align_to(row_act, prev, CANOPUS_TARGET_ALIGN_OUT_BOTTOM_MID, 0, CANOPUS_TARGET_ROW_GAP);
        canopus_fw_lv_obj_add_event_cb(row_act, on_vibrate_click, CANOPUS_TARGET_EVENT_CLICKED, 0);
    }

    /* Log all pointers into ui_probe.txt */
    {
        typedef int (*open_fn)(const char *, int, ...);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        int fd = open_file("/data/canopus/ui_probe.txt", 2 | 4 | 0x200, 0666);
        if (fd >= 0) {
            const char *msg1 = "UI_PAGE_ACTIVE_OK root=";
            while (*msg1) *p++ = *msg1++;
            format_hex(hex, (uintptr_t)root);
            for (int k = 0; hex[k]; k++) *p++ = hex[k];

            const char *msg2 = " title=";
            while (*msg2) *p++ = *msg2++;
            format_hex(hex, (uintptr_t)title);
            for (int k = 0; hex[k]; k++) *p++ = hex[k];

            const char *msg3 = " content=";
            while (*msg3) *p++ = *msg3++;
            format_hex(hex, (uintptr_t)content);
            for (int k = 0; hex[k]; k++) *p++ = hex[k];

            const char *msg4 = " row1=";
            while (*msg4) *p++ = *msg4++;
            format_hex(hex, (uintptr_t)row_dev);
            for (int k = 0; hex[k]; k++) *p++ = hex[k];

            const char *msg5 = " act=";
            while (*msg5) *p++ = *msg5++;
            format_hex(hex, (uintptr_t)row_act);
            for (int k = 0; hex[k]; k++) *p++ = hex[k];

            *p++ = '\n';
            (void)canopus_fw_write(fd, probe_buf, (uint32_t)(p - probe_buf));
            (void)canopus_fw_close(fd);
        }
    }

    return 0;
}

static int demo_on_resume(struct firmware_page_descriptor *page)
{
    typedef int (*open_fn)(const char *, int, ...);
    open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
    int fd;
    (void)page;

    fd = open_file("/data/canopus/ui_probe.txt", 2 | 4, 0666);
    if (fd >= 0) {
        const char *msg = "UI_PAGE_ACTIVE_OK\n";
        (void)canopus_fw_write(fd, msg, (uint32_t)canopus_strlen(msg));
        (void)canopus_fw_close(fd);
    }
    return 0;
}

static int demo_on_pause(struct firmware_page_descriptor *page)
{
    (void)page;
    return 0;
}

static int demo_on_destroy(struct firmware_page_descriptor *page)
{
    (void)page;
    return 0;
}

static int32_t demo_prepare(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;
    return 0;
}

static int32_t demo_activate(const struct canopus_context_v1 *ctx)
{
    s_ctx = ctx;

    /* 1. Query System Object */
    const char *model = "Xiaomi Band 10 Pro";
    uint32_t batt = 85u;
    if (ctx->system != 0) {
        if (ctx->system->device_model != 0) {
            model = ctx->system->device_model(ctx);
        }
        if (ctx->system->battery_level != 0) {
            batt = ctx->system->battery_level(ctx);
        }
    }
    s_batt_level = batt;

    /* 2. Persistent state via KV Object */
    if (ctx->kv != 0) {
        int32_t runs = 0;
        if (ctx->kv->get_int != 0) {
            runs = ctx->kv->get_int(ctx, "run_count", 0);
        }
        runs++;
        s_run_count = (uint32_t)runs;
        if (ctx->kv->set_int != 0) {
            ctx->kv->set_int(ctx, "run_count", runs);
        }
    }

    /* 3. File System Object */
    if (ctx->fs != 0 && ctx->fs->open != 0 && ctx->fs->write != 0 && ctx->fs->close != 0) {
        int32_t fd = ctx->fs->open(ctx, "demo_status.txt", CANOPUS_FS_WRITE | CANOPUS_FS_CREATE | CANOPUS_FS_TRUNC);
        if (fd >= 0) {
            char logbuf[128];
            char runs_str[16], batt_str[16];
            char *p = logbuf;
            format_u32(runs_str, s_run_count);
            format_u32(batt_str, batt);

            const char *k1 = "STATUS=ACTIVE RUNS=";
            while (*k1) *p++ = *k1++;
            for (int k = 0; runs_str[k]; k++) *p++ = runs_str[k];
            const char *k2 = " BATT=";
            while (*k2) *p++ = *k2++;
            for (int k = 0; batt_str[k]; k++) *p++ = batt_str[k];
            const char *k3 = " MODEL=";
            while (*k3) *p++ = *k3++;
            for (int k = 0; model[k] && p < logbuf + 110; k++) *p++ = model[k];
            *p++ = '\n';

            (void)ctx->fs->write(ctx, fd, logbuf, (uint32_t)(p - logbuf));
            (void)ctx->fs->close(ctx, fd);
        }
    }

    /* 4. Write probe file into /data/canopus/probe.txt for test harness */
    {
        typedef int (*open_fn)(const char *, int, ...);
        open_fn open_file = (open_fn)(uintptr_t)CANOPUS_SUP_NUTTX_OPEN;
        int fd = open_file("/data/canopus/probe.txt", 2 | 4, 0666);
        if (fd >= 0) {
            char text[32];
            char runs_str[16];
            char *p = text;
            format_u32(runs_str, s_run_count);
            const char *tag = "native_demo:";
            while (*tag) *p++ = *tag++;
            for (int k = 0; runs_str[k]; k++) *p++ = runs_str[k];
            *p++ = '\n';
            (void)canopus_fw_write(fd, text, (uint32_t)(p - text));
            (void)canopus_fw_close(fd);
        }
    }

    /* 5. Logging Object */
    if (ctx->log != 0 && ctx->log->info != 0) {
        ctx->log->info(ctx, "native_demo activated");
    }

    /* 6. Haptic Feedback Object */
    if (ctx->haptics != 0 && ctx->haptics->vibrate_preset != 0) {
        ctx->haptics->vibrate_preset(ctx, CANOPUS_HAPTIC_CLICK);
    }

    return 0;
}

static int32_t demo_deactivate(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    return 0;
}

static int32_t demo_stop(const struct canopus_context_v1 *ctx)
{
    (void)ctx;
    s_ctx = 0;
    return 0;
}

static int32_t demo_query(struct canopus_status_writer_v1 *writer)
{
    (void)writer;
    return 0;
}

static int32_t demo_publish_native_app_stage(const struct canopus_context_v1 *ctx, uint32_t stage)
{
    if (stage == 1u) {
        void *pages[1];
        struct canopus_app_entry_v1 entry;
        int32_t app_id;
        if (!canopus_services_has(ctx, CANOPUS_SVC_LAUNCHER, 1) ||
            ctx->launcher == 0 || ctx->launcher->register_app == 0) {
            return CANOPUS_SVC_ERR_UNSUPPORTED;
        }
        if (s_demo_app_id != 0u) return 0;

        canopus_memset(&s_demo_page, 0, sizeof(s_demo_page));
        s_demo_page.page_name = (void *)"demo_main";
        s_demo_page.page_id = 0u;
        s_demo_page.on_signal = (void *)demo_on_signal;
        s_demo_page.on_create = (void *)demo_on_create;
        s_demo_page.on_resume = (void *)demo_on_resume;
        s_demo_page.on_pause = (void *)demo_on_pause;
        s_demo_page.on_destroy = (void *)demo_on_destroy;

        pages[0] = &s_demo_page;
        entry.struct_size = sizeof(entry);
        entry.label = s_display_name;
        entry.icon = s_launcher_icon;
        entry.pages = pages;
        entry.page_count = 1u;
        app_id = ctx->launcher->register_app(ctx, &entry);
        if (app_id < 0) return app_id;
        s_demo_app_id = (uint16_t)app_id;
        return 0;
    }
    if (stage == 2u) {
        if (s_demo_app_id == 0u) return CANOPUS_SVC_ERR_STATE;
        /* The Supervisor drains registration before this later publication
         * stage. Navigate using the identity it assigned to our native page. */
        (void)canopus_fw_page_goto((uint32_t)s_demo_app_id << 16, 0u, 0u, 0u);
        return 0;
    }
    return CANOPUS_SVC_ERR_ARGUMENT;
}

static const struct canopus_module_descriptor_v1 s_descriptor = {
    sizeof(struct canopus_module_descriptor_v1),
    CANOPUS_ABI_MAJOR,
    CANOPUS_ABI_MINOR,
    CANOPUS_FLAG_HAS_NATIVE_APP | CANOPUS_FLAG_REGISTERS_LAUNCHER_ENTRY |
        CANOPUS_FLAG_APP_UNREGISTER_REBOOT_REQUIRED,
    DEMO_MODULE_ID,
    "1.0.0",
    "native-demo",
    CANOPUS_TARGET_ID,
    demo_prepare,
    demo_activate,
    demo_deactivate,
    demo_stop,
    demo_query,
    0,
    demo_publish_native_app_stage,
};

CANOPUS_MODULE_ENTRY(s_descriptor)
