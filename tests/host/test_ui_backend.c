/* Execute the production backend against counted firmware primitives, without
 * firmware images. The Python runner supplies empty target headers and includes
 * an unmodified copy of the backend via CANOPUS_UI_HOST_SOURCE. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "canopus_ui_host.h"

struct counts {
    unsigned create, row, text, style, source, size, flags, align, range, value;
    unsigned events, destroy, dialog, scale, info, font_create, font_release, font_set, font_wait;
};
static struct counts calls;
struct widget {
    unsigned char native[128];
    void *parent;
    int x, y;
    uint32_t props[128];
    unsigned flags, checked;
    int width, height, minimum, maximum, value, scroll_y;
    unsigned deleted, font_px, scale_x, scale_y, source_width, source_height;
    char text[256], secondary[256], source[256];
    void (*callback)(void *);
    void *event_user;
    void (*delete_callback)(void *);
    void *delete_user;
    void *font;
};
static struct widget widgets[256];
static unsigned allocated;
static int fail_next_create;
static unsigned fail_create_at;
static unsigned image_width = 200, image_height = 100;
static int image_decode_failure;
struct mock_font { struct mock_font *next; uint16_t pixels; unsigned pending; };
static unsigned native_fonts, font_bindings_allocated;
static int fail_font_create, fail_font_alloc, fail_font_event, font_cpu_active;
static void *font_root_slot;
static unsigned char font_root_storage[48];
static int font_manager_identity;
#ifndef CANOPUS_TEST_NO_FONT
static struct mock_font *mock_fonts;
static void *font_draw_units;
static unsigned char font_vg_unit[48], font_sw_unit[48], font_display_list[16];
static unsigned char font_display[808], font_layer[120];
static void font_native_pointer(void *base, uint32_t offset, void *value)
{ memcpy((uint8_t *)base + offset,&value,sizeof(value)); }
static void font_set_cpu_active(int active)
{
    font_cpu_active = active;
    font_native_pointer(font_vg_unit,32,active ? &font_manager_identity : NULL);
}
static void *font_alloc(uint32_t size)
{
    void *memory;
    if (fail_font_alloc) { fail_font_alloc = 0; return NULL; }
    memory = malloc(size); assert(memory != NULL); font_bindings_allocated++;
    return memory;
}
static void font_free(void *memory)
{ assert(memory != NULL && font_bindings_allocated != 0); font_bindings_allocated--; free(memory); }
static void *font_create(void *manager, const void *key_data)
{
    const struct { const char *family; uint16_t pixels, style; } *key = key_data;
    struct mock_font *font;
    assert(manager == &font_manager_identity && strcmp(key->family,"MiSans-Medium") == 0 && key->pixels != 0 && key->style == 0);
    calls.font_create++;
    if (fail_font_create) { fail_font_create = 0; return NULL; }
    font = malloc(sizeof(*font)); assert(font != NULL);
    font->pixels = key->pixels; font->pending = 0; native_fonts++;
    font->next = mock_fonts; mock_fonts = font;
    return font;
}
static void font_release(void *manager, void *native)
{
    struct mock_font *font = native;
    unsigned i;
    assert(manager == &font_manager_identity && native_fonts != 0 && font->pending == 0);
    for (i = 0; i < allocated; i++) assert(widgets[i].deleted || widgets[i].font != font);
    {
        struct mock_font **link = &mock_fonts;
        while (*link != font) link = &(*link)->next;
        *link = font->next;
    }
    calls.font_release++; native_fonts--; free(font);
}
static void font_wait(void *unit)
{
    struct mock_font *font;
    assert(unit == font_vg_unit && !font_cpu_active); calls.font_wait++;
    for (font = mock_fonts; font != NULL; font = font->next) font->pending = 0;
}
static void *font_event_user(void *event) { return *(void **)event; }
#define FW_UI_FONT_CREATE font_create
#define FW_UI_FONT_RELEASE font_release
#define FW_UI_FONT_WAIT font_wait
#define FW_UI_FONT_MANAGER_SLOT (&font_root_slot)
#define FW_UI_FONT_DRAW_UNITS (&font_draw_units)
#define FW_UI_FONT_DISPLAY_LIST font_display_list
#define FW_UI_FONT_VG_DISPATCH 1u
#define FW_UI_FONT_SW_DISPATCH 2u
#define FW_UI_FONT_EVENT_USER_DATA font_event_user
#define FW_UI_FONT_EVENT_DELETE 36u
#define CANOPUS_SUP_HEAP_MALLOC font_alloc
#define CANOPUS_SUP_HEAP_FREE font_free
#endif

static void text_copy(char *out, const char *text)
{
    snprintf(out, 256, "%s", text != NULL ? text : "");
}
static void *new_widget(void *parent)
{
    if (fail_next_create || (fail_create_at != 0u && allocated == fail_create_at)) {
        fail_next_create = 0; fail_create_at = 0; return NULL;
    }
    assert(allocated < sizeof(widgets) / sizeof(widgets[0]));
    calls.create++;
    widgets[allocated].parent = parent;
    widgets[allocated].flags = 0x100000u;
    widgets[allocated].scale_x = widgets[allocated].scale_y = 256;
    widgets[allocated].props[16] = widgets[allocated].props[17] = 7;
    widgets[allocated].props[18] = widgets[allocated].props[19] = 7;
    return &widgets[allocated++];
}
static void *create_label(void *parent)
{
    struct widget *label = new_widget(parent);
    if (label != NULL) label->height = 32;
    return label;
}
static void add_flag(void *object, uint32_t flag)
{ assert(!((struct widget *)object)->deleted); calls.flags++; ((struct widget *)object)->flags |= flag; }
static void clear_flag(void *object, uint32_t flag)
{ assert(!((struct widget *)object)->deleted); calls.flags++; ((struct widget *)object)->flags &= ~flag; }
static void set_size(void *object, int32_t width, int32_t height)
{
    calls.size++;
    ((struct widget *)object)->width = width;
    ((struct widget *)object)->height = height;
}
static void align_to(void *object, void *base, uint32_t align, int32_t x, int32_t y)
{
    struct widget *w = object;
    /* Native align_to positions relative to the visible parent rectangle. */
    if (base != NULL && base == w->parent && align == 0u)
        y += ((struct widget *)base)->scroll_y;
    w->x=x; w->y=y; calls.align++;
}
#ifndef CANOPUS_MGR_BAND11
static void align_object(void *object, uint32_t align, int32_t x, int32_t y)
{ align_to(object, NULL, align, x, y); }
#endif
static void *add_event(void *object, void (*callback)(void *), uint32_t code, void *user)
{
    struct widget *widget = object;
    calls.events++;
    /* Row-label identity tokens (see row_label) register but store nothing. */
    if (object != NULL && (uintptr_t)object <= 1024u) return object;
    if (code == 36u) {
        if (fail_font_event) { fail_font_event = 0; return NULL; }
        widget->delete_callback = callback; widget->delete_user = user;
    }
    else { widget->callback = callback; widget->event_user = user; }
    return object;
}
static void set_text(void *object, const char *text)
{ calls.text++; text_copy(((struct widget *)object)->text, text); }
static void set_style(void *object, const void *style, uint32_t opacity, uint32_t selector)
{
    (void)opacity; (void)selector; calls.style++;
    ((struct widget *)object)->font_px = (unsigned)(uintptr_t)style;
}
static void set_source(void *object, const void *source)
{
    struct widget *image = object;
    calls.source++; text_copy(image->source, source);
    image->source_width = image_width; image->source_height = image_height;
}
#ifndef CANOPUS_TEST_NO_IMAGE_FIT
static int image_info(const void *source, void *out)
{
    uint32_t header[3] = {0};
    uint16_t width = (uint16_t)image_width, height = (uint16_t)image_height;
    assert(source != NULL); calls.info++;
    if (image_decode_failure) return 0;
    memcpy((unsigned char *)header + 4, &width, sizeof(width));
    memcpy((unsigned char *)header + 6, &height, sizeof(height));
    memcpy(out,header,sizeof(header));
    return 1;
}
static void image_scale(void *object, int32_t x, int32_t y)
{
    struct widget *image = object;
    assert(x > 0 && x <= UINT16_MAX && y > 0 && y <= UINT16_MAX);
    calls.scale++; image->scale_x = (unsigned)x; image->scale_y = (unsigned)y;
}
#define FW_UI_IMAGE_INFO image_info
#define FW_UI_IMAGE_SCALE image_scale
#endif
static void set_range(void *object, int32_t minimum, int32_t maximum)
{
    calls.range++;
    ((struct widget *)object)->minimum = minimum;
    ((struct widget *)object)->maximum = maximum;
}
static void set_value(void *object, int32_t value, uint32_t animation)
{ (void)animation; calls.value++; ((struct widget *)object)->value = value; }
static void delete_object(void *object)
{
    struct widget *w = object;
    unsigned i;
    assert(!w->deleted);
    if (w->delete_callback != NULL) {
        void *user = w->delete_user;
        w->delete_callback(&user);
        w->delete_callback = NULL;
    }
    for (i = 0; i < allocated; i++)
        if (!widgets[i].deleted && widgets[i].parent == w) delete_object(&widgets[i]);
    w->deleted = 1;
    calls.destroy++;
}
static int32_t get_scroll_y(void *object)
{ assert(!((struct widget *)object)->deleted); return ((struct widget *)object)->scroll_y; }
static void scroll_to_y(void *object, int32_t y, uint32_t animation)
{
    struct widget *w = object;
    int32_t extent = w->height;
    unsigned i;
    assert(!w->deleted && animation == 0);
    for (i = 0; i < allocated; i++) {
        struct widget *child = &widgets[i];
        if (!child->deleted && child->parent == w && !(child->flags & 1u) &&
            child->y + child->height > extent) extent = child->y + child->height;
    }
    if (y < 0) y = 0;
    if (y > extent - w->height) y = extent - w->height;
    w->scroll_y = y;
}
static uint32_t native_time(void) { return 1000; }
static void *event_indev(void *event) { return event; }
static void indev_point(void *indev, int32_t *point)
{ (void)indev; point[0] = 50; point[1] = 100; }
static void object_coords(void *object, int32_t *area)
{ (void)object; area[0] = 0; area[1] = 0; area[2] = 335; area[3] = 479; }
#define FW_UI_OBJECT_GET_SCROLL_Y get_scroll_y
#define FW_UI_OBJECT_SCROLL_TO_Y scroll_to_y
#define FW_UI_TICK_GET native_time
#define FW_UI_COORD_TYPE int32_t
#define FW_UI_EVENT_GET_INDEV event_indev
#define FW_UI_INDEV_GET_POINT indev_point
#define FW_UI_OBJECT_GET_COORDS object_coords
#define FW_UI_EVENT_PRESSED 101u
#define FW_UI_EVENT_PRESSING 102u
#define FW_UI_EVENT_LONG_PRESSED 103u
#define FW_UI_EVENT_RELEASED 104u
#define FW_UI_EVENT_PRESS_LOST 105u
#define FW_UI_EVENT_SCROLL 106u
static void *create_dialog(void *root, void *page)
{ (void)page; return new_widget(root); }
static void dialog_content(void *object, uint32_t mode, const char *title,
                           const char *message, const char *icon,
                           const struct canopus_ui_host_msgbox_button *buttons, uint32_t count)
{
    (void)mode; (void)title; (void)icon; (void)buttons; assert(count == 2);
    calls.dialog++; text_copy(((struct widget *)object)->text, message);
}
static void row_content(void *object, const char *primary, const char *secondary, uint8_t selected)
{
    struct widget *w = object;
    text_copy(w->text, primary); text_copy(w->secondary, secondary); w->checked = selected;
}
#ifdef CANOPUS_MGR_BAND11
static struct widget row_labels[1024];
static unsigned row_labels_used;
/* Stock content labels live in their own pool: they are not host widgets, so
   widget counts and create-failure injection stay exact. */
static void *row_label(void *row, uint32_t offset, int required)
{
    /* The firmware stores 32-bit object pointers at these fields, four bytes
       apart. On this 64-bit harness the stored values are small identity
       tokens (the backend only ever compares them, as with event targets). */
    uint32_t token = 0;
    memcpy(&token, (unsigned char *)row + offset, sizeof(token));
    if (token == 0 && required) {
        assert(row_labels_used < sizeof(row_labels) / sizeof(row_labels[0]));
        row_labels[row_labels_used++].parent = row;
        token = row_labels_used;
        memcpy((unsigned char *)row + offset, &token, sizeof(token));
    }
    return (void *)(uintptr_t)token;
}
#endif
#ifndef CANOPUS_MGR_BAND11
struct title_event { uint32_t code; void *user; };
static void *event_user(void *event) { return ((struct title_event *)event)->user; }
static void *create_title(void *root, const char *text, uint32_t mode,
                           void (*back)(void *), void *user)
{
    struct widget *w = new_widget(root);
    if (w != NULL) {
        text_copy(w->text, text); w->callback = back; w->flags = mode; w->event_user = user;
    }
    return w;
}
static void *create_row(void *root, const char *primary, const char *secondary, uint8_t trailing)
{
    void *w = new_widget(root);
    (void)trailing;
    if (w != NULL) row_content(w, primary, secondary, 0);
    return w;
}
static void *row_trailing(void *object) { return object; }
static int update_row(void *object, const void *icon, const char *primary,
                      const char *secondary, int32_t value, uint8_t selected)
{
    (void)icon; (void)value; calls.row++;
    row_content(object, primary, secondary, selected); return 0;
}
static uint32_t event_code(void *event) { return *(uint32_t *)event; }
static void *create_image(void *parent, const void *theme)
{ assert(theme == NULL); return new_widget(parent); }
#define FW_UI_IMAGE_CREATE_THEME_ARG 1
#define FW_LVX_CONTENT_CREATE new_widget
#define FW_LVX_OBJECT_ALIGN align_object
#define FW_LVX_LIST_ROW_CREATE create_row
#define FW_LVX_LIST_ROW_TRAILING row_trailing
#define FW_LVX_LIST_ROW_UPDATE update_row
#define FW_LVX_EVENT_GET_CODE event_code
#define FW_LVX_EVENT_GET_USER_DATA event_user
#define FW_LVX_PAGE_TITLE_CREATE create_title
#define FW_LVX_OBJECT_ADD_FLAG add_flag
#define FW_LVX_OBJECT_CLEAR_FLAG clear_flag
#define FW_LVX_OBJECT_DELETE delete_object
#define FW_LVX_OBJECT_SET_SIZE set_size
#define FW_LVX_ALIGN_TO align_to
#define FW_LVX_EVENT_ADD add_event
#define FW_LVX_LABEL_CREATE create_label
#define FW_LVX_LABEL_SET_TEXT set_text
#define FW_LVX_IMAGE_CREATE create_image
#define FW_LVX_IMAGE_SET_SRC set_source
#define FW_LVX_BAR_CREATE new_widget
#define FW_LVX_BAR_SET_RANGE set_range
#define FW_LVX_BAR_SET_VALUE set_value
#else
#define FW_LVX_TITLE_ICON_OFFSET 0x34u
#define FW_LVX_TITLE_LABEL_OFFSET 0x38u
static void *create_title(void *root, const char *text, uint32_t mode, void (*back)(void *))
{
    struct widget *w = new_widget(root);
    if (w != NULL) {
        void *child = new_widget(w);
        text_copy(w->text, text); w->callback = back; w->flags = mode;
        /* The firmware binds the back callback to a child, not the title root. */
        memcpy(w->native + FW_LVX_TITLE_LABEL_OFFSET, &child, sizeof(child));
    }
    return w;
}
static void *add_content(void *object, const void *icon, const char *primary,
                          const char *secondary, int32_t value, uint8_t selected, uint8_t trailing)
{
    (void)icon; (void)value; (void)trailing;
    row_content(object, primary, secondary, selected);
    /* The stock item keeps its content objects at +0x3c/+0x40 and the trailing
       element at +0x54; the labels are separate clickable objects. Keep them
       in their own pool so widget counts and create-failure injection stay
       exact. */
    row_label(object, 0x3c, 1);
    row_label(object, 0x40, secondary != NULL && secondary[0] != 0);
    memcpy((unsigned char *)object + 0x54, &object, sizeof(object));
    return object;
}
static void update_row(void *object, const void *icon, const char *primary,
                        const char *secondary, int32_t value, uint8_t selected)
{
    (void)icon; (void)value; calls.row++; row_content(object, primary, secondary, selected);
    /* The firmware creates the secondary label lazily on the first non-empty
       text and hides it again when empty. */
    row_label(object, 0x3c, 1);
    row_label(object, 0x40, secondary != NULL && secondary[0] != 0);
}
static void remove_styles(void *object)
{ memset(((struct widget *)object)->props,0,sizeof(((struct widget *)object)->props)); calls.style++; }
static void set_padding(void *object, int32_t padding, uint32_t selector)
{ (void)object; (void)padding; (void)selector; calls.style++; }
static void set_long_mode(void *object, uint32_t mode)
{ (void)object; (void)mode; calls.style++; }
#define FW_LV_OBJECT_CREATE new_widget
#define FW_LV_OBJECT_REMOVE_STYLES remove_styles
#define FW_LV_OBJECT_PAD_TOP set_padding
#define FW_LV_OBJECT_PAD_BOTTOM set_padding
#define FW_LV_LABEL_LONG_MODE set_long_mode
#define FW_LV_PAGE_TITLE_CREATE create_title
#define FW_LVX_LIST_ITEM_CREATE new_widget
#define FW_LVX_LIST_ITEM_ADD_CONTENT add_content
#define FW_LVX_LIST_ITEM_UPDATE update_row
#define FW_LVX_LIST_TRAILING_OFFSET 0x54u
#define FW_LVX_LIST_PRIMARY_OFFSET 0x3cu
#define FW_LVX_LIST_SECONDARY_OFFSET 0x40u
#define FW_LV_OBJECT_ADD_FLAG add_flag
#define FW_LV_OBJECT_CLEAR_FLAG clear_flag
#define FW_LV_OBJECT_DELETE delete_object
#define FW_LV_OBJECT_SET_SIZE set_size
#define FW_LV_ALIGN_TO align_to
#define FW_LV_EVENT_ADD add_event
#define FW_LV_LABEL_CREATE create_label
#define FW_LV_LABEL_SET_TEXT set_text
#define FW_LV_IMAGE_CREATE new_widget
#define FW_LV_IMAGE_SET_SRC set_source
#define FW_LV_BAR_CREATE new_widget
#define FW_LV_BAR_SET_RANGE set_range
#define FW_LV_BAR_SET_VALUE set_value
#endif
static void set_property(void *object, uint32_t prop, uintptr_t value, uint32_t selector)
{
    struct widget *widget = object;
    (void)selector; assert(prop < 128);
    if (prop == 90u) {
        assert(value != 0);
        widget->font = (void *)value;
        widget->font_px = ((struct mock_font *)widget->font)->pixels;
        calls.font_set++;
    } else { calls.style++; widget->props[prop] = (uint32_t)value; }
}
#define FW_UI_STYLE_SET set_property
#define FW_UI_STYLE_RADIUS 12u
#define FW_UI_STYLE_CLIP_CORNER 45u
#define FW_UI_STYLE_BORDER_COLOR 49u
#define FW_UI_STYLE_BORDER_WIDTH 48u
#define FW_UI_STYLE_OPA 95u
#define FW_LVX_STYLE_APPLY set_style
#define FW_STYLE_MISANS_MEDIUM_28 28u
#define FW_STYLE_MISANS_MEDIUM_24 24u
#define FW_LVX_MSGBOX_CREATE create_dialog
#define FW_LVX_MSGBOX_SET_CONTENT dialog_content
#include CANOPUS_UI_HOST_SOURCE

static struct canopus_ui_host_view view;
static struct canopus_ui_context_v1 context;
static unsigned event_calls;
static uint32_t last_key, last_event;
static struct canopus_ui_event_v1 last_input;
static int capture_on_press, detach_on_cancel, destroy_root_on_cancel, destroy_root_before_detach;
static int32_t receive_event(void *cookie, const struct canopus_ui_event_v1 *event)
{
    (void)cookie; assert(event->generation == context.committed.generation);
    event_calls++; last_key = event->key; last_event = event->event_id; last_input = *event;
    if (capture_on_press && event->type == CANOPUS_UI_EVENT_PRESS)
        assert(canopus_ui_control(&context,event->generation,event->key,CANOPUS_UI_CONTROL_CAPTURE,0,0,NULL) == 0);
    if (detach_on_cancel && event->type == CANOPUS_UI_EVENT_CANCEL) {
        void *root = view.root;
        assert(((struct widget *)host_node(&view,10)->object)->flags & 16u);
        /* Firmware destroys the page-owned native subtree on close; both
         * close orderings are legal platform sequences. */
        if (destroy_root_on_cancel && destroy_root_before_detach) delete_object(root);
        canopus_ui_host_detach(&view);
        if (destroy_root_on_cancel && !destroy_root_before_detach) delete_object(root);
    }
    return 0;
}
static void reset(void)
{
    unsigned i;
    canopus_ui_host_detach(&view);
    for (i = 0; i < allocated; i++)
        if (!widgets[i].deleted && widgets[i].parent == NULL) delete_object(&widgets[i]);
    font_cpu_active = 0;
    canopus_ui_host_poll(0);
    assert(native_fonts == 0 && font_bindings_allocated == 0);
    font_root_slot = font_root_storage;
    {
        void *manager = &font_manager_identity;
        memcpy(font_root_storage + 28,&manager,sizeof(manager));
    }
    fail_font_create = fail_font_alloc = fail_font_event = 0;
#ifndef CANOPUS_TEST_NO_FONT
    memset(font_vg_unit,0,sizeof(font_vg_unit)); memset(font_sw_unit,0,sizeof(font_sw_unit));
    memset(font_display_list,0,sizeof(font_display_list)); memset(font_display,0,sizeof(font_display)); memset(font_layer,0,sizeof(font_layer));
    font_draw_units = font_vg_unit;
    font_native_pointer(font_vg_unit,0,font_sw_unit);
    { uintptr_t vg = 1, sw = 2; memcpy(font_vg_unit + 16,&vg,sizeof(vg)); memcpy(font_sw_unit + 16,&sw,sizeof(sw)); }
    font_native_pointer(font_display_list,4,font_display);
    font_native_pointer(font_display,680,font_layer);
#endif
    memset(widgets, 0, sizeof(widgets)); allocated = 0; fail_next_create = 0; fail_create_at = 0;
    event_calls = 0; last_key = last_event = 0;
    image_width = 200; image_height = 100; image_decode_failure = 0;
    capture_on_press = detach_on_cancel = destroy_root_on_cancel = destroy_root_before_detach = 0;
    memset(&last_input,0,sizeof(last_input));
    canopus_ui_host_attach(&view, new_widget(NULL), NULL, NULL, 0);
    assert(canopus_ui_context_init(&context, &canopus_ui_host_backend, &view,
                                  receive_event, NULL) == 0);
    canopus_ui_host_bind(&view, &context);
    canopus_ui_host_set_interactive(&view, 1);
    memset(&calls, 0, sizeof(calls));
}
static unsigned mutations(void)
{
    return calls.create + calls.row + calls.text + calls.style + calls.source + calls.size +
           calls.flags + calls.align + calls.range + calls.value + calls.events + calls.destroy + calls.dialog + calls.scale +
           calls.font_set;
}
static void clear_counts(void) { memset(&calls, 0, sizeof(calls)); }
static void print_counts(const char *label)
{
    printf("%s: total=%u row=%u text=%u style=%u image=%u size=%u flags=%u align=%u value=%u\n",
           label, mutations(), calls.row, calls.text, calls.style, calls.source,
           calls.size, calls.flags, calls.align, calls.value);
}
static struct canopus_ui_tree_v1 *begin(void)
{
    struct canopus_ui_tree_v1 *tree;
    assert(canopus_ui_tree_begin(&context, &tree) == 0);
    assert(CANOPUS_UI_NAVIGATION_PAGE(tree, 1, "Manager") == 0);
    return tree;
}
static int finish(struct canopus_ui_tree_v1 *tree)
{ assert(canopus_ui_end(tree) == 0); return canopus_ui_tree_commit(tree); }
static void row(struct canopus_ui_tree_v1 *tree, uint32_t key, const char *text, uint32_t event)
{
    struct canopus_ui_action_row_props_v1 props = {
        sizeof(props), text, (uint32_t)strlen(text), "Details", 7, event, 1
    };
    assert(canopus_ui_action_row(tree, key, &props) == 0);
}
struct page_options {
    const char *row_text, *label_text, *image_path;
    int image_width, image_height, progress;
    uint32_t image_id, row_event;
    unsigned omit_media, reverse_rows, omit_second;
};
static struct page_options options(void)
{
    struct page_options o = { "Theme A", "Description", "/data/theme.bin", 48, 48, 25, 1, 101, 0, 0, 0 };
    return o;
}
static int page(struct page_options o)
{
    struct canopus_ui_tree_v1 *tree = begin();
    if (o.reverse_rows && !o.omit_second) row(tree, 3, "Theme B", 102);
    row(tree, 2, o.row_text, o.row_event);
    if (!o.reverse_rows && !o.omit_second) row(tree, 3, "Theme B", 102);
    if (!o.omit_media) {
        struct canopus_ui_text_props_v1 text = { sizeof(text), o.label_text, (uint32_t)strlen(o.label_text), CANOPUS_UI_TEXT_BODY };
        struct canopus_ui_component_props_v1 image = {
            sizeof(image), o.image_path, (uint32_t)strlen(o.image_path), "", 0, 0,
            CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0, o.image_id
        };
        struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
        assert(canopus_ui_text(tree, 4, &text) == 0);
        assert(canopus_ui_component(tree, 5, CANOPUS_UI_NODE_IMAGE, &image) == 0);
        layout.width = (int16_t)o.image_width; layout.height = (int16_t)o.image_height;
        assert(canopus_ui_node_set_layout(tree, 5, &layout) == 0);
        assert(CANOPUS_UI_PROGRESS(tree, 6, o.progress, 0, 100) == 0);
    }
    return finish(tree);
}
static void emit_row_target(unsigned slot, uint32_t code, void **field)
{
#ifndef CANOPUS_MGR_BAND11
    struct widget *w = *field;
    (void)slot;
    w->callback(&code);
#else
    /* Native events have 32-bit pointers at +4 and a code at +8. On a 64-bit
     * host those overlap; only equality is used by the backend, so use an
     * opaque matching token and restore the real binding after dispatch. */
    unsigned char event[24] = {0};
    void *previous = *field, *token;
    memcpy(event + 4, &previous, sizeof(previous));
    memcpy(event + 8, &code, sizeof(uint16_t));
    memcpy(&token, event + 4, sizeof(token));
    *field = token;
    host_dispatch_row(slot, event);
    if (*field == token) *field = previous;
#endif
}
static void emit_row(unsigned slot, uint32_t code)
{ emit_row_target(slot, code, &view.nodes[slot].event_object); }
#ifdef CANOPUS_MGR_BAND11
static int row_page(const char *text, const char *detail, uint32_t event_id)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_action_row_props_v1 props = {
        sizeof(props), text, (uint32_t)strlen(text),
        detail, (uint32_t)strlen(detail), event_id, 1
    };
    assert(canopus_ui_action_row(tree, 2, &props) == 0);
    return finish(tree);
}
/* The stock row's title/description are separate clickable objects: a hit on
   the row's text must bind exactly like the row surface, a hit elsewhere must
   stay silent, and a lazily created secondary label joins the set. */
static void test_row_content_hits(void)
{
    unsigned slot;
    struct canopus_ui_host_node *node;
    reset();
    assert(row_page("Title", "", 301) == 0);
    slot = (unsigned)(host_node(&view, 2) - view.nodes);
    node = &view.nodes[slot];
    assert(node->hit_extra[0] != NULL && node->hit_extra[1] == NULL);
    clear_counts();
    emit_row_target(slot, HOST_EVENT_CLICKED, &node->hit_extra[0]);
    assert(event_calls == 1 && last_key == 2 && last_event == 301);
    /* A target outside the registered set is not this row's business. */
    {
        unsigned char event[24] = {0};
        uint16_t code = (uint16_t)HOST_EVENT_CLICKED;
        void *foreign = view.content_root;
        memcpy(event + 4, &foreign, sizeof(foreign));
        memcpy(event + 8, &code, sizeof(code));
        host_dispatch_row(slot, event);
        assert(event_calls == 1);
    }
    /* The firmware creates the secondary label lazily on the first non-empty
       detail text; the update adopts it as a hit target. */
    assert(row_page("Title", "Details", 301) == 0);
    node = &view.nodes[slot];
    assert(node->hit_extra[1] != NULL);
    emit_row_target(slot, HOST_EVENT_CLICKED, &node->hit_extra[1]);
    assert(event_calls == 2 && last_key == 2 && last_event == 301);
}
#endif
static void test_identical_and_binding(int baseline)
{
    struct page_options o = options();
    reset(); assert(page(o) == 0); print_counts("initial"); clear_counts();
    assert(page(o) == 0); print_counts("identical");
    if (baseline) return;
    assert(mutations() == 0);
    o.row_event = 777; clear_counts(); assert(page(o) == 0); assert(mutations() == 0);
    emit_row((unsigned)(host_node(&view,2)-view.nodes), HOST_EVENT_CLICKED);
    assert(event_calls == 1 && last_key == 2 && last_event == 777);
}
static void test_property_updates(void)
{
    struct page_options o = options();
    reset(); assert(page(o) == 0); clear_counts();
    o.row_text = "Changed"; assert(page(o) == 0);
    assert(calls.row == 1 && calls.align == 0 && mutations() == 1);
    clear_counts(); o.label_text = "Different description"; assert(page(o) == 0);
    assert(calls.text == 1 && calls.style == 0 && calls.row == 0 && calls.source == 0);
    /* Layout only moves objects whose measured bounds changed. */
    clear_counts(); o.image_path = "/data/other.bin"; assert(page(o) == 0);
    assert(calls.source == 1 && calls.size == 0 && calls.text == 0 && calls.row == 0);
    clear_counts(); o.image_id++; assert(page(o) == 0);
    assert(calls.source == 1 && calls.size == 0 && calls.align == 0);
    clear_counts(); o.image_width++; assert(page(o) == 0);
    assert(calls.size == 1 && calls.source == 0 && calls.text == 0 && calls.row == 0);
    clear_counts(); o.progress++; assert(page(o) == 0);
    assert(calls.value == 1 && mutations() == 1);
}
static int clipped_page(int clip)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    assert(CANOPUS_UI_SECTION(tree,10,"") == 0);
    layout.width = 160; layout.height = 80;
    style.corner_radius = 16; style.opacity = 500;
    visual.flags = CANOPUS_UI_VISUAL_FLAG_OPACITY | (clip ? CANOPUS_UI_VISUAL_FLAG_CLIP : 0u);
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    assert(canopus_ui_node_set_style(tree,10,&style) == 0);
    assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    row(tree,11,"Clipped child",401);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_clip_toggle(void)
{
    struct widget *parent, *child;
    reset(); assert(clipped_page(0) == 0);
    parent = host_node(&view,10)->object; child = host_node(&view,11)->object;
    assert(child->parent == parent && child->height > parent->height);
    assert(parent->props[FW_UI_STYLE_RADIUS] == 16u);
    assert(parent->props[FW_UI_STYLE_OPA] == 500u * 255u / 1000u);
    assert(parent->props[FW_UI_STYLE_CLIP_CORNER] == 0u);
    assert(clipped_page(1) == 0);
    assert(host_node(&view,10)->object == parent && host_node(&view,11)->object == child);
    assert(parent->props[FW_UI_STYLE_CLIP_CORNER] == 1u);
    clear_counts(); assert(clipped_page(1) == 0); assert(mutations() == 0);
    assert(clipped_page(0) == 0);
    assert(host_node(&view,10)->object == parent && host_node(&view,11)->object == child);
    assert(parent->props[FW_UI_STYLE_CLIP_CORNER] == 0u);
    assert(parent->props[FW_UI_STYLE_RADIUS] == 16u);
}
static void test_reorder_and_reappearance(void)
{
    struct page_options o = options(); void *first, *second;
    reset(); assert(page(o) == 0);
    first=host_node(&view,2)->object; second=host_node(&view,3)->object;
    clear_counts(); o.reverse_rows=1; assert(page(o)==0);
    assert(calls.create==0 && calls.row==0 && calls.align>0);
    assert(host_node(&view,2)->object==first && host_node(&view,3)->object==second);
    clear_counts(); o.omit_media=1; o.omit_second=1; assert(page(o)==0);
    assert(host_node(&view,3)==NULL && host_node(&view,4)==NULL);
    clear_counts(); assert(page(o)==0); assert(mutations()==0);
    o.omit_media=0; o.omit_second=0; assert(page(o)==0);
    assert(host_node(&view,2)->object==first && host_node(&view,3)!=NULL);
}
static void test_failure_retry(void)
{
    struct page_options old=options(), proposed;
    reset(); assert(page(old)==0); proposed=old;
    proposed.row_text="Must not apply"; proposed.image_width=0;
    clear_counts(); assert(page(proposed)<0); assert(mutations()==0);
    assert(strcmp(((struct widget *)host_node(&view,2)->object)->text,old.row_text)==0);
    clear_counts(); assert(page(old)==0); assert(mutations()==0);
}
static int switch_page(int enabled)
{
    struct canopus_ui_tree_v1 *tree = begin();
    assert(CANOPUS_UI_SWITCH_ROW(tree, 2, "Enabled", "", 201, 0, enabled) == 0);
    return finish(tree);
}
static void test_switch_events(void)
{
    struct widget *w;
    reset(); assert(switch_page(0) == 0); w = host_node(&view,2)->object;
    w->checked = 1; clear_counts(); emit_row((unsigned)(host_node(&view,2)-view.nodes), HOST_EVENT_VALUE_CHANGED);
    assert(event_calls == 0 && w->checked == 0 && calls.row == 1);
    clear_counts(); assert(switch_page(0) == 0); assert(mutations() == 0);
    assert(switch_page(1) == 0);
    emit_row((unsigned)(host_node(&view,2)-view.nodes), HOST_EVENT_VALUE_CHANGED);
    assert(event_calls == 1 && last_key == 2 && last_event == 201);
    assert(last_input.type == CANOPUS_UI_EVENT_VALUE_CHANGED);
}
static int dialog_page(const char *message, uint32_t event)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_text_props_v1 text = {
        sizeof(text), message, (uint32_t)strlen(message), CANOPUS_UI_TEXT_BODY
    };
    assert(CANOPUS_UI_DIALOG(tree, 8, "Confirm", "") == 0);
    assert(canopus_ui_text(tree, 9, &text) == 0);
    assert(CANOPUS_UI_BUTTON(tree, 10, "Cancel", event, 1) == 0);
    assert(CANOPUS_UI_BUTTON(tree, 11, "Delete", 302, 1) == 0);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_dialog_binding(void)
{
    void *box;
    reset(); assert(dialog_page("Delete resource?", 301) == 0); box = view.dialog.native_box;
    clear_counts(); assert(dialog_page("Delete resource?", 303) == 0);
    assert(mutations() == 0 && view.dialog.native_box == box);
    clear_counts(); assert(dialog_page("Delete another resource?", 303) == 0);
    assert(calls.destroy == 1 && calls.create == 1 && calls.dialog == 1);
    assert(strcmp(((struct widget *)view.dialog.native_box)->text, "Delete another resource?") == 0);
    view.dialog.buttons[0].callback(NULL);
    assert(event_calls == 1 && last_key == 10 && last_event == 303);
}
static void test_dialog_allocation_failure_preserves_state(void)
{
    struct canopus_ui_host_binding binding;
    struct canopus_ui_tree_v1 *tree;
    struct widget *item, *parent, *box;
    struct host_input_state *state;
    uint32_t generation;
    unsigned i;
    reset(); tree = begin();
    for (i = 0; i < 6; i++) row(tree,20+i,"Old page item",401+i);
    assert(finish(tree) == 0);
    item = host_node(&view,20)->object; parent = view.content_root;
    binding = host_node(&view,20)->binding; capture_on_press = 1;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
    state = host_input_state(&view); generation = context.committed.generation;
    fail_next_create = 1; clear_counts();
    assert(dialog_page("Delete resource?",301) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && context.committed.generation == generation && view.render_valid);
    assert(state->pressed && state->captured && event_calls == 1);
    assert(!item->deleted && !parent->deleted && !(parent->flags & 16u));
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_MOVE,110,9,12) == 0);
    assert(last_input.key == 20 && last_input.delta_x == 4 && last_input.delta_y == 4);
    assert(dialog_page("Delete resource?",301) == 0);
    assert(last_input.type == CANOPUS_UI_EVENT_CANCEL && !state->pressed && !state->captured);
    box = view.dialog.native_box; generation = context.committed.generation;
    fail_next_create = 1; clear_counts();
    assert(dialog_page("Delete another resource?",301) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && context.committed.generation == generation && view.render_valid);
    assert(view.dialog.native_box == box && view.dialog.active && !box->deleted);
    clear_counts(); assert(dialog_page("Delete resource?",301) == 0); assert(mutations() == 0);
}
static void test_nested_header_does_not_displace_rows(void)
{
    struct canopus_ui_tree_v1 *tree;
    struct widget *first, *second;
    /* show_back is 0 while the id is reserved: a hidden back keeps its handler. */
    struct canopus_ui_navigation_header_props_v1 header = {
        sizeof(header), "Native UI Lab", 13, "ABI 2", 5, 5, 0, 1, 0
    };
    reset();
    tree = begin();
    assert(canopus_ui_navigation_header(tree, 2, &header) == 0);
    assert(canopus_ui_end(tree) == 0); /* header */
    row(tree, 10, "Card & overlay", 11);
    row(tree, 11, "100 items", 12);
    assert(finish(tree) == 0); /* page */
    first = host_node(&view, 10)->object;
    second = host_node(&view, 11)->object;
    /* The titlebar is chrome, not a flow box: rows must start at the top of the
     * content instead of one content-height lower. */
    assert(first->y == 0);
    assert(second->y == HOST_ROW_HEIGHT + HOST_ROW_GAP);
}
static void emit_title(struct widget *title)
{
#ifndef CANOPUS_MGR_BAND11
    struct title_event event = { HOST_EVENT_CLICKED, title->event_user };
    title->callback(&event);
#else
    unsigned char event[24] = {0};
    void *target;
    memcpy(&target, title->native + FW_LVX_TITLE_LABEL_OFFSET, sizeof(target));
    memcpy(event + 4, &target, sizeof(target));
    title->callback(event);
#endif
}
static int header_page(const char *title, uint32_t event, uint32_t back)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_navigation_header_props_v1 header = {
        sizeof(header), title, (uint32_t)strlen(title), "", 0, event, back, 0, 0
    };
    assert(canopus_ui_navigation_header(tree, 2, &header) == 0);
    assert(canopus_ui_end(tree) == 0);
    row(tree, 10, "Body", 401);
    return finish(tree);
}
static unsigned back_calls;
static void receive_back(struct canopus_ui_host_view *owner)
{ assert(owner == &view); back_calls++; }
static const struct canopus_ui_host_hooks back_hooks = { .back = receive_back };
static void test_header_back_binding(void)
{
    struct widget *old, *current;
    reset(); back_calls = 0;
    assert(header_page("Route A", 501, 1) == 0);
    old = view.page_title;
    assert(old->callback != NULL);
    emit_title(old);
    assert(event_calls == 1 && last_key == 2 && last_event == 501);
    /* The root page must not replace an explicit header on identical commits. */
    clear_counts(); assert(header_page("Route A", 501, 1) == 0);
    assert(mutations() == 0 && view.page_title == old);
    emit_title(old);
    assert(event_calls == 2);
    assert(header_page("Route B", 502, 1) == 0);
    current = view.page_title;
    assert(current != old);
    emit_title(old); assert(event_calls == 2);
    emit_title(current); assert(event_calls == 3 && last_event == 502);
    /* Rebinding an unchanged title also invalidates its queued native event. */
    assert(header_page("Route B", 503, 1) == 0);
    assert(view.page_title != current);
    emit_title(current); assert(event_calls == 3);
    current = view.page_title;
    view.hooks = &back_hooks;
    emit_title(current);
    assert(event_calls == 4 && last_event == 503 && back_calls == 0);
    canopus_ui_host_set_interactive(&view, 0);
    emit_title(current); assert(event_calls == 4);
    canopus_ui_host_set_interactive(&view, 1);
    assert(header_page("Route B", 503, 0) == 0);
    emit_title(current); assert(event_calls == 4 && back_calls == 0);
    assert(((struct widget *)view.page_title)->flags == 0);
    /* A page without an explicit header retains the page-lifecycle hook. */
    assert(page(options()) == 0);
    emit_title(view.page_title);
    assert(event_calls == 4 && back_calls == 1);
}
static void emit_content(uint32_t code)
{
    struct widget *content = view.content_root;
    assert(content->callback != NULL);
#ifndef CANOPUS_MGR_BAND11
    struct title_event event = {code,content->event_user};
    content->callback(&event);
#else
    unsigned char event[24] = {0};
    void *previous = view.content_root, *token;
    memcpy(event + 4,&previous,sizeof(previous));
    memcpy(event + 8,&code,sizeof(uint16_t));
    memcpy(&token,event + 4,sizeof(token));
    view.content_root = token;
    content->callback(event);
    view.content_root = previous;
#endif
}
static void test_page_scroll_and_absolute_overflow(void)
{
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_scroll_metrics_v1 metrics;
    struct widget *content;
    reset();
    tree = begin(); row(tree,10,"Offscreen",401);
    visual.flags = CANOPUS_UI_VISUAL_FLAG_ABSOLUTE; visual.y = 500;
    assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    assert(finish(tree) == 0);
    content = view.content_root;
    assert(content->flags & 16u);
    assert(canopus_ui_control(&context,context.committed.generation,1,CANOPUS_UI_CONTROL_SCROLL_QUERY,0,0,&metrics) == 0);
    assert(metrics.content_height == 500 + HOST_ROW_HEIGHT);
    emit_content(FW_UI_EVENT_SCROLL);
    assert(last_input.type == CANOPUS_UI_EVENT_SCROLL && last_input.key == 1);
    emit_content(FW_UI_EVENT_PRESSED);
    emit_content(FW_UI_EVENT_RELEASED);
    assert(last_input.type == CANOPUS_UI_EVENT_RELEASE && last_input.key == 1);
}
static int virtual_page(int32_t y, uint16_t type)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_component_props_v1 props = {
        sizeof(props),"",0,"",0,701,CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,0
    };
    assert(canopus_ui_component(tree,10,type,&props) == 0);
    layout.height = 100;
    visual.flags = CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT;
    visual.content_height = 900; visual.scroll_y = y;
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    row(tree,20,"Visible item",702);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_identical_commit_preserves_capture_without_mutation(void)
{
    unsigned scenario;
    for (scenario = 0; scenario < 3; scenario++) {
        struct canopus_ui_host_binding original, binding;
        struct canopus_ui_host_node *ancestor;
        struct page_options small = options();
        uint32_t generation, capture_key = scenario == 1 ? 10 : 20;
        unsigned repeat;
        reset();
        small.omit_media = small.omit_second = 1;
        if (scenario == 2) {
            capture_key = 2;
            assert(page(small) == 0);
            ancestor = host_node(&view,1);
            assert(ancestor->scroll_locked);
        } else {
            assert(virtual_page(0,CANOPUS_UI_NODE_SCROLL) == 0);
            ancestor = host_node(&view,10);
        }
        original = host_node(&view,scenario == 2 ? 2 : 20)->binding;
        assert(host_input_route(&view,&original,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
        assert(canopus_ui_control(&context,original.generation,capture_key,CANOPUS_UI_CONTROL_CAPTURE,0,0,NULL) == 0);
        assert(host_input_state(&view)->pressed && host_input_state(&view)->captured);
        assert(!( ((struct widget *)ancestor->object)->flags & 16u));
        generation = context.committed.generation;
        for (repeat = 0; repeat < 2; repeat++) {
            clear_counts();
            if (scenario == 2) assert(page(small) == 0);
            else assert(virtual_page(0,CANOPUS_UI_NODE_SCROLL) == 0);
            assert(mutations() == 0);
            assert(context.committed.generation > generation && event_calls == 1);
            assert(host_input_state(&view)->pressed && host_input_state(&view)->captured);
            assert(!( ((struct widget *)ancestor->object)->flags & 16u));
            generation = context.committed.generation;
        }
        {
            struct canopus_ui_host_node *pressed = host_node(&view,original.key);
            struct widget *item = pressed->object;
            text_copy(item->text,"Native dirty");
            pressed->dirty = 1;
            clear_counts();
            if (scenario == 2) assert(page(small) == 0);
            else assert(virtual_page(0,CANOPUS_UI_NODE_SCROLL) == 0);
            assert(calls.row == 1 && strcmp(item->text,scenario == 2 ? small.row_text : "Visible item") == 0);
            assert(!pressed->dirty && host_input_state(&view)->captured && event_calls == 1);
            assert(!( ((struct widget *)ancestor->object)->flags & 16u));
            generation = context.committed.generation;
            clear_counts();
            if (scenario == 2) assert(page(small) == 0);
            else assert(virtual_page(0,CANOPUS_UI_NODE_SCROLL) == 0);
            assert(mutations() == 0 && event_calls == 1);
            generation = context.committed.generation;
        }
        assert(host_input_route(&view,&original,CANOPUS_UI_EVENT_MOVE,110,9,12) == CANOPUS_UI_ERR_STALE);
        binding = host_node(&view,original.key)->binding;
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_MOVE,110,9,12) == 0);
        assert(last_input.key == capture_key && last_input.generation == generation);
        assert(last_input.delta_x == 4 && last_input.delta_y == 4);
        assert(canopus_ui_control(&context,generation,capture_key,CANOPUS_UI_CONTROL_RELEASE_CAPTURE,0,0,NULL) == 0);
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_RELEASE,120,9,12) == 0);
        assert(event_calls == 3 && last_input.key == original.key);
        assert(last_input.type == CANOPUS_UI_EVENT_RELEASE && last_input.generation == generation);
        assert(!host_input_state(&view)->pressed && !host_input_state(&view)->captured);
        if (scenario == 2) assert(!( ((struct widget *)ancestor->object)->flags & 16u));
        else assert(((struct widget *)ancestor->object)->flags & 16u);
    }
}
static void test_virtual_native_bounds_and_capture_replacement(void)
{
    struct widget *old_parent, *old_item;
    struct canopus_ui_host_binding binding;
    reset(); assert(virtual_page(400,CANOPUS_UI_NODE_SCROLL) == 0);
    old_parent = host_node(&view,10)->object;
    assert(old_parent->scroll_y == 400);
    clear_counts(); assert(virtual_page(400,CANOPUS_UI_NODE_SCROLL) == 0);
    assert(mutations() == 0);
    assert(virtual_page(0,CANOPUS_UI_NODE_SCROLL) == 0);
    assert(old_parent->scroll_y == 0);
    assert(canopus_ui_control(&context,context.committed.generation,10,CANOPUS_UI_CONTROL_SCROLL_TO,800,0,NULL) == 0);
    assert(old_parent->scroll_y == 800);
    old_item = host_node(&view,20)->object;
    binding = host_node(&view,20)->binding; capture_on_press = 1;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
    assert(!(old_parent->flags & 16u));
    fail_next_create = 1;
    assert(virtual_page(0,CANOPUS_UI_NODE_LIST) == CANOPUS_UI_ERR_BACKEND);
    assert(!old_parent->deleted && !(old_parent->flags & 16u));
    assert(virtual_page(0,CANOPUS_UI_NODE_LIST) == 0);
    assert(old_parent->deleted && old_item->deleted);
    assert(!( ((struct widget *)host_node(&view,10)->object)->flags & 16u));
    binding = host_node(&view,20)->binding;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_MOVE,110,9,12) == 0);
    assert(last_input.delta_x == 4 && last_input.delta_y == 4);
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_RELEASE,120,9,12) == 0);
    assert(((struct widget *)host_node(&view,10)->object)->flags & 16u);
}
static int virtual_window(unsigned first, int32_t y)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_component_props_v1 props = {
        sizeof(props),"",0,"",0,701,CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,0
    };
    unsigned i;
    assert(canopus_ui_component(tree,10,CANOPUS_UI_NODE_SCROLL,&props) == 0);
    layout.height = 100;
    visual.flags = CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT;
    visual.content_height = 7200; visual.scroll_y = y;
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    for (i=first;i<first+3;i++) {
        row(tree,100+i,"Window item",1000+i);
        visual = (struct canopus_ui_visual_v1)CANOPUS_UI_VISUAL_DEFAULT;
        visual.flags = CANOPUS_UI_VISUAL_FLAG_ABSOLUTE;
        visual.y = (int16_t)(i*72);
        assert(canopus_ui_node_set_visual(tree,100+i,&visual) == 0);
    }
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_virtual_window_placement_while_scrolled(void)
{
    struct widget *parent, *item;
    reset(); assert(virtual_window(0,0) == 0);
    parent = host_node(&view,10)->object;
    scroll_to_y(parent,720,0);
    assert(virtual_window(10,720) == 0);
    item = host_node(&view,110)->object;
    assert(item->y == 720 && item->y-parent->scroll_y == 0);
    assert(canopus_ui_control(&context,context.committed.generation,110,CANOPUS_UI_CONTROL_TRANSLATE,5,12,NULL) == 0);
    assert(item->x == 5 && item->y-parent->scroll_y == 12);
    clear_counts(); assert(virtual_window(10,720) == 0);
    assert(mutations() == 0);
}
static int route_page(const char *title, unsigned count, uint32_t page_key)
{
    struct canopus_ui_tree_v1 *tree;
    assert(canopus_ui_tree_begin(&context,&tree) == 0);
    assert(CANOPUS_UI_NAVIGATION_PAGE(tree,page_key,"Manager") == 0);
    struct canopus_ui_navigation_header_props_v1 header = {
        sizeof(header),title,(uint32_t)strlen(title),"",0,5,1,0,0
    };
    unsigned i;
    assert(canopus_ui_navigation_header(tree,2,&header) == 0);
    assert(canopus_ui_end(tree) == 0);
    for (i=0;i<count;i++) row(tree,10+i,"Route item",401+i);
    return finish(tree);
}
static void test_header_keeps_page_scroll_and_route_resets(void)
{
    struct widget *content;
    reset(); assert(route_page("Selections",5,1) == 0);
    content = view.content_root;
    assert(content->height == HOST_CONTENT_HEIGHT && (content->flags & 16u));
    scroll_to_y(content,60,0); assert(content->scroll_y == 60);
    assert(route_page("Selections",5,1) == 0); assert(content->scroll_y == 60);
    assert(route_page("Saved selections",5,1) == 0); assert(content->scroll_y == 60);
    assert(route_page("Home",4,3) == 0); assert(content->scroll_y == 0);
}
static int32_t replace_home_rows(void *cookie, const struct canopus_ui_event_v1 *event)
{
    struct canopus_ui_tree_v1 *tree;
    unsigned i;
    (void)cookie;
    if (event->type != CANOPUS_UI_EVENT_ACTIVATE) return 0;
    tree = begin();
    assert(CANOPUS_UI_SECTION(tree,10,"") == 0);
    row(tree,11,"Child A",501); row(tree,12,"Child B",502);
    assert(canopus_ui_end(tree) == 0);
    assert(CANOPUS_UI_SECTION(tree,13,"") == 0);
    for (i=0;i<2;i++) row(tree,14+i,"New child",503+i);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_reentrant_reused_keys_delete_old_rows(void)
{
    struct widget *old[4];
    unsigned i;
    reset(); assert(route_page("Home",4,1) == 0);
    for (i=0;i<4;i++) old[i]=host_node(&view,10+i)->object;
    context.event_handler = replace_home_rows;
    emit_row((unsigned)(host_node(&view,10)-view.nodes),HOST_EVENT_CLICKED);
    for (i=0;i<4;i++) assert(old[i]->deleted);
    assert(host_node(&view,13)->type == CANOPUS_UI_NODE_SECTION);
    assert(host_node(&view,13)->object != old[3]);
    assert(host_previous(&view) != NULL);
}
static int two_headers_page(void)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_navigation_header_props_v1 h1 = {
        sizeof(h1), "HeaderOne", 9, "", 0, 0, 0, 0, 0 };
    struct canopus_ui_navigation_header_props_v1 h2 = {
        sizeof(h2), "HeaderTwo", 9, "", 0, 0, 0, 0, 0 };
    assert(canopus_ui_navigation_header(tree, 2, &h1) == 0);
    assert(canopus_ui_end(tree) == 0);
    assert(canopus_ui_navigation_header(tree, 3, &h2) == 0);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
/* Two navigation headers share one page_title: the first header drives it and
 * the binding; identical re-commits are keep-alive (no title churn). */
static void test_first_header_drives_shared_title(void)
{
    struct widget *title;
    reset();
    assert(two_headers_page() == 0);
    title = view.page_title;
    assert(title != NULL && strcmp(title->text,"HeaderOne") == 0);
    assert(view.title_binding.key == 2);
    clear_counts();
    assert(two_headers_page() == 0);
    assert(view.page_title == title && mutations() == 0);
}
static int label_bounds_page(uint16_t foreground, uint16_t font)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_text_props_v1 text = {sizeof(text),"Hello",5,CANOPUS_UI_TEXT_BODY};
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    layout.width = 100; layout.height = 50;
    style.foreground = foreground; visual.font_px = font;
    assert(canopus_ui_text(tree,10,&text) == 0);
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    assert(canopus_ui_node_set_style(tree,10,&style) == 0);
    {
        int32_t result = canopus_ui_node_set_visual(tree,10,&visual);
        if (result != 0) { canopus_ui_tree_abort(tree); return result; }
    }
    return finish(tree);
}
static void test_label_style_preserves_bounds(void)
{
    struct widget *label;
    reset(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,0) == 0);
    label = host_node(&view,10)->object;
    assert(label->width == 100 && label->height == 50);
    assert(label_bounds_page(CANOPUS_UI_COLOR_DANGER,28) == 0);
    assert(host_node(&view,10)->object == label);
    assert(label->width == 100 && label->height == 50);
    assert(label->props[88] == host_color(CANOPUS_UI_COLOR_DANGER));
    clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_DANGER,28) == 0);
    assert(mutations() == 0);
    assert(label_bounds_page(CANOPUS_UI_COLOR_SUCCESS,24) == 0);
    assert(label->width == 100 && label->height == 50);
    clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_SUCCESS,24) == 0);
    assert(mutations() == 0);
    assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,24) == 0);
    assert(label->deleted && host_node(&view,10)->object != label);
    label = host_node(&view,10)->object;
    assert(label->width == 100 && label->height == 50 && label->props[88] == 0);
    clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,24) == 0);
    assert(mutations() == 0);
}
static void test_font_size_contract(void)
{
    static const uint16_t unsupported[] = {1,17,21,26,30,32,48,UINT16_MAX};
    struct widget *label;
    uint32_t generation;
    unsigned i;
    (void)generation;
    reset(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,0) == 0);
    label = host_node(&view,10)->object;
    assert(label->font_px == 24);
    assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,28) == 0);
    assert(label->font_px == 28);
    assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,24) == 0);
    assert(label->font_px == 24);
    generation = context.committed.generation;
    for (i = 0; i < sizeof(unsupported)/sizeof(unsupported[0]); i++) {
        clear_counts();
#ifdef CANOPUS_TEST_NO_FONT
        assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,unsupported[i]) == CANOPUS_UI_ERR_UNSUPPORTED);
        assert(mutations() == 0 && context.committed.generation == generation);
        assert(host_node(&view,10)->object == label && label->font_px == 24);
#else
        assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,unsupported[i]) == 0);
        label = host_node(&view,10)->object;
        assert(label->font_px == unsupported[i] && calls.font_set == 1 && calls.font_create == 1);
        clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,unsupported[i]) == 0);
        assert(mutations() == 0 && calls.font_create == 0 && calls.font_release == 0);
#endif
    }
    clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,0) == 0);
    label = host_node(&view,10)->object;
    assert(calls.style != 0 && label->font_px == 24);
    clear_counts(); assert(label_bounds_page(CANOPUS_UI_COLOR_INHERIT,0) == 0);
    assert(mutations() == 0);
}
#ifndef CANOPUS_TEST_NO_FONT
static void font_text(struct canopus_ui_tree_v1 *tree, uint32_t key, uint16_t pixels, int visible)
{
    struct canopus_ui_component_props_v1 text = {
        sizeof(text),"Sized text",10,"",0,0,visible ? CANOPUS_UI_NODE_FLAG_VISIBLE : 0,0,0,0,0,0
    };
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    assert(canopus_ui_component(tree,key,CANOPUS_UI_NODE_RICH_TEXT,&text) == 0);
    visual.font_px = pixels;
    assert(canopus_ui_node_set_visual(tree,key,&visual) == 0);
}
static int font_page(uint16_t first, uint16_t second, int visible, unsigned count, uint32_t route)
{
    struct canopus_ui_tree_v1 *tree;
    assert(canopus_ui_tree_begin(&context,&tree) == 0);
    assert(CANOPUS_UI_NAVIGATION_PAGE(tree,route,"Fonts") == 0);
    if (count > 0) font_text(tree,10,first,visible);
    if (count > 1) font_text(tree,11,second,visible);
    return finish(tree);
}
static void test_font_cache_and_visibility(void)
{
    struct widget *first, *second;
    struct mock_font *font;
    reset(); assert(font_page(21,21,1,2,1) == 0);
    first = host_node(&view,10)->object; second = host_node(&view,11)->object;
    font = first->font;
    assert(font == second->font && calls.font_create == 1 && calls.font_set == 2 && native_fonts == 1);
    clear_counts(); assert(font_page(21,21,1,2,1) == 0);
    assert(mutations() == 0 && calls.font_create == 0 && calls.font_release == 0);
    clear_counts(); assert(font_page(21,21,0,2,1) == 0);
    assert((first->flags & 1u) && (second->flags & 1u));
    assert(native_fonts == 1 && calls.font_create == 0 && calls.font_release == 0);
    clear_counts(); assert(font_page(21,21,1,2,1) == 0);
    assert(!(first->flags & 1u) && first->font == font && second->font == font);
    assert(calls.font_create == 0 && calls.font_release == 0);
    clear_counts(); assert(font_page(21,21,1,1,1) == 0);
    assert(second->deleted && !first->deleted && native_fonts == 1 && calls.font_release == 0);
    font->pending = 1;
    clear_counts(); assert(font_page(21,21,1,0,1) == 0);
    assert(first->deleted && native_fonts == 0 && calls.font_release == 1 && calls.font_wait != 0);
    assert(s_font_labels == NULL && s_fonts == NULL && font_bindings_allocated == 0);
}
static void test_font_dialog_retains_live_body(void)
{
    struct widget *label;
    struct mock_font *font;
    reset(); assert(font_page(21,28,1,1,1) == 0);
    label = host_node(&view,10)->object; font = label->font;
    clear_counts(); assert(dialog_page("Keep body",301) == 0);
    assert(!label->deleted && label->font == font && native_fonts == 1);
    assert(calls.font_release == 0 && calls.font_create == 0);
    clear_counts(); assert(dialog_page("Keep body",301) == 0);
    assert(mutations() == 0 && calls.font_create == 0);
    assert(font_page(21,28,1,1,1) == 0);
    assert(host_node(&view,10)->object == label && label->font == font && native_fonts == 1);
    clear_counts(); assert(font_page(24,28,1,1,1) == 0);
    assert(label->deleted && native_fonts == 0 && calls.font_release == 1);
    label = host_node(&view,10)->object;
    assert(label->font == NULL && label->font_px == 24);
}
static void test_font_page_reload_and_view_reset(void)
{
    unsigned ordering;
    for (ordering = 0; ordering < 2; ordering++) {
        struct widget *root, *label;
        struct mock_font *font;
        reset(); assert(font_page(17,32,1,2,1) == 0);
        label = host_node(&view,10)->object; font = label->font;
        clear_counts(); assert(font_page(17,32,1,2,2) == 0);
        assert(label->deleted && native_fonts == 2 && calls.font_create == 0 && calls.font_release == 0);
        label = host_node(&view,10)->object;
        assert(label->font == font);
        root = view.root;
        ((struct mock_font *)label->font)->pending = 1;
        clear_counts();
        if (ordering == 0) { delete_object(root); canopus_ui_host_detach(&view); }
        else { canopus_ui_host_detach(&view); memset(&view,0xa5,sizeof(view)); delete_object(root); memset(&view,0,sizeof(view)); }
        assert(native_fonts == 2); /* DELETE retires; it does not free in the destructor. */
        font_set_cpu_active(1); canopus_ui_host_poll(1);
        assert(native_fonts == 2 && calls.font_release == 0);
        font_set_cpu_active(0); canopus_ui_host_poll(2);
        assert(native_fonts == 0 && s_font_labels == NULL && s_fonts == NULL && font_bindings_allocated == 0);
    }
    reset(); assert(font_page(26,28,1,1,1) == 0);
    {
        struct widget *root = view.root;
        canopus_ui_host_attach(&view,new_widget(NULL),NULL,NULL,0);
        assert(native_fonts == 1); /* New view storage must not free the old native label's font. */
        delete_object(root); canopus_ui_host_poll(3);
        assert(native_fonts == 0 && s_font_labels == NULL);
    }
}
static int font_surface_page(uint16_t section_px, uint16_t check_px, unsigned unsupported_kind)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_component_props_v1 check = {
        sizeof(check),"Check",5,"Sub",3,801,CANOPUS_UI_NODE_FLAG_ENABLED|CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,0
    };
    if (unsupported_kind == 1) { visual.font_px = 21; assert(canopus_ui_node_set_visual(tree,1,&visual) == 0); }
    else if (unsupported_kind == 2) {
        row(tree,12,"Action",802); visual.font_px = 21; assert(canopus_ui_node_set_visual(tree,12,&visual) == 0);
    } else if (unsupported_kind == 3) {
        assert(CANOPUS_UI_SECTION(tree,13,"") == 0);
        visual.font_px = 21; assert(canopus_ui_node_set_visual(tree,13,&visual) == 0);
        assert(canopus_ui_end(tree) == 0);
    } else if (unsupported_kind == 4) {
        struct canopus_ui_component_props_v1 dialog = {sizeof(dialog),"",0,"",0,0,CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,0};
        struct canopus_ui_text_props_v1 msg = {sizeof(msg),"Ask",3,CANOPUS_UI_TEXT_BODY};
        assert(canopus_ui_component(tree,20,CANOPUS_UI_NODE_DIALOG,&dialog) == 0);
        assert(canopus_ui_text(tree,21,&msg) == 0);
        visual.font_px = 21; assert(canopus_ui_node_set_visual(tree,21,&visual) == 0);
        row(tree,22,"Yes",803); row(tree,23,"No",804);
        assert(canopus_ui_end(tree) == 0);
    } else {
        assert(CANOPUS_UI_SECTION(tree,10,"Heading") == 0);
        visual.font_px = section_px; assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
        assert(canopus_ui_component(tree,11,CANOPUS_UI_NODE_CHECKBOX,&check) == 0);
        visual.font_px = check_px; assert(canopus_ui_node_set_visual(tree,11,&visual) == 0);
        assert(canopus_ui_end(tree) == 0);
    }
    return finish(tree);
}
static void test_font_surfaces_and_unsupported_nodes(void)
{
    unsigned kind;
    uint32_t generation;
    reset(); assert(font_surface_page(21,26,0) == 0);
    assert(((struct widget *)host_node(&view,10)->primary)->font_px == 21);
    assert(((struct widget *)host_node(&view,11)->primary)->font_px == 26);
    assert(((struct widget *)host_node(&view,11)->secondary)->font_px == 24);
    assert(native_fonts == 2);
    generation = context.committed.generation;
    for (kind = 1; kind <= 4; kind++) {
        clear_counts(); assert(font_surface_page(21,26,kind) == CANOPUS_UI_ERR_UNSUPPORTED);
        assert(mutations() == 0 && context.committed.generation == generation && native_fonts == 2);
    }
    assert(font_surface_page(28,24,0) == 0);
    assert(((struct widget *)host_node(&view,10)->primary)->font_px == 28);
    assert(((struct widget *)host_node(&view,11)->primary)->font_px == 24);
    assert(native_fonts == 0);
}
static void test_font_many_sizes_do_not_accumulate(void)
{
    uint16_t pixels;
    reset();
    for (pixels = 1; pixels <= 600; pixels++) {
        assert(font_page(pixels,pixels,1,1,1) == 0);
        assert(native_fonts == (pixels == 24 || pixels == 28 ? 0u : 1u));
        assert(s_font_count == native_fonts);
    }
    assert(font_page(0,0,1,0,1) == 0);
    assert(native_fonts == 0 && font_bindings_allocated == 0 && s_font_labels == NULL);
}
static void test_font_retirement_boundaries(void)
{
    unsigned boundary;
    for (boundary = 0; boundary < 4; boundary++) {
        struct widget *root;
        struct mock_font *font;
        uintptr_t dispatch;
        reset(); assert(font_page(21,21,1,1,1) == 0);
        root = view.root; font = ((struct widget *)host_node(&view,10)->object)->font;
        font->pending = 1;
        delete_object(root); canopus_ui_host_detach(&view);
        if (boundary == 0) font_display[58] = 2;
        if (boundary == 1) font_native_pointer(font_layer,100,&font_manager_identity);
        if (boundary == 2) font_set_cpu_active(1);
        if (boundary == 3) { dispatch = 3; memcpy(font_sw_unit + 16,&dispatch,sizeof(dispatch)); }
        clear_counts(); canopus_ui_host_poll(1);
        assert(native_fonts == 1 && font->pending == 1 && calls.font_release == 0 && calls.font_wait == 0);
        font_display[58] = 0; font_native_pointer(font_layer,100,NULL); font_set_cpu_active(0);
        dispatch = 2; memcpy(font_sw_unit + 16,&dispatch,sizeof(dispatch));
        canopus_ui_host_poll(2);
        assert(native_fonts == 0 && font_bindings_allocated == 0 && calls.font_release == 1 && calls.font_wait == 1);
    }
}
static void test_font_preparation_failures(void)
{
    struct widget *label;
    uint32_t generation;
    unsigned allocations;
    reset(); assert(font_page(21,21,1,1,1) == 0);
    label = host_node(&view,10)->object; generation = context.committed.generation;
    allocations = font_bindings_allocated;
    fail_font_create = 1;
    clear_counts(); assert(font_page(26,21,1,1,1) == CANOPUS_UI_ERR_BACKEND);
    assert(context.committed.generation == generation && !label->deleted && label->font_px == 21);
    assert(calls.font_set == 0 && calls.font_release == 0 && native_fonts == 1 && font_bindings_allocated == allocations);
    fail_font_alloc = 1;
    clear_counts(); assert(font_page(21,21,1,2,1) == CANOPUS_UI_ERR_BACKEND);
    assert(context.committed.generation == generation && !label->deleted && calls.font_set == 0);
    assert(native_fonts == 1 && font_bindings_allocated == allocations);
    fail_font_event = 1;
    clear_counts(); assert(font_page(21,21,1,2,1) == CANOPUS_UI_ERR_BACKEND);
    assert(context.committed.generation == generation && !label->deleted && calls.font_set == 0);
    assert(native_fonts == 1 && font_bindings_allocated == allocations);
    fail_next_create = 1;
    clear_counts(); assert(font_page(26,21,1,2,1) == CANOPUS_UI_ERR_BACKEND);
    assert(context.committed.generation == generation && !label->deleted && calls.font_set == 0);
    assert(calls.font_create == 1 && calls.font_release == 1 && native_fonts == 1 && font_bindings_allocated == allocations);
}
#endif
static int styled_section_page(const struct canopus_ui_style_v1 *style,
                               const struct canopus_ui_visual_v1 *visual,
                               const char *heading)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_section_props_v1 section = {sizeof(section),heading,(uint32_t)strlen(heading)};
    assert(canopus_ui_section(tree,10,&section) == 0);
    assert(canopus_ui_node_set_style(tree,10,style) == 0);
    assert(canopus_ui_node_set_visual(tree,10,visual) == 0);
    row(tree,11,"Child",401);
    assert(canopus_ui_end(tree) == 0);
    row(tree,12,"Unaffected",402);
    return finish(tree);
}
static void test_inherited_style_rebuilds_subtree(void)
{
    const struct canopus_ui_style_v1 defaults = CANOPUS_UI_STYLE_DEFAULT;
    const struct canopus_ui_visual_v1 inherited = CANOPUS_UI_VISUAL_DEFAULT;
    unsigned property;
    for (property = 0; property < 12; property++) {
        struct canopus_ui_style_v1 style = defaults;
        struct canopus_ui_visual_v1 visual = inherited;
        struct widget *parent, *child, *sibling;
        uint32_t generation;
        switch (property) {
        case 0: style.foreground = CANOPUS_UI_COLOR_DANGER; break;
        case 1: style.background = CANOPUS_UI_COLOR_DANGER; break;
        case 2: style.corner_radius = 8; break;
        case 3: style.border_color = CANOPUS_UI_COLOR_DANGER; break;
        case 4: style.border_width = 3; break;
        case 5: style.opacity = 500; break;
        case 6: style.variant = CANOPUS_UI_VARIANT_DESTRUCTIVE; break;
        case 7: style.variant = CANOPUS_UI_VARIANT_TONAL; break;
        case 8: style.variant = CANOPUS_UI_VARIANT_OUTLINED; break;
        case 9: visual.flags = CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB; visual.foreground_rgb = 0x123456; break;
        case 10: visual.flags = CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB | CANOPUS_UI_VISUAL_FLAG_BORDER_RGB; visual.background_rgb = 0x234567; visual.border_rgb = 0x345678; break;
        case 11: visual.flags = CANOPUS_UI_VISUAL_FLAG_OPACITY; break;
        }
        reset(); assert(styled_section_page(&style,&visual,"Heading") == 0);
        parent = host_node(&view,10)->object; child = host_node(&view,11)->object;
        sibling = host_node(&view,12)->object; generation = context.committed.generation;
        fail_next_create = 1;
        assert(styled_section_page(&defaults,&inherited,"Heading") == CANOPUS_UI_ERR_BACKEND);
        assert(context.committed.generation == generation && view.render_valid);
        assert(host_node(&view,10)->object == parent && !parent->deleted && !child->deleted);
        assert(styled_section_page(&defaults,&inherited,"Heading") == 0);
        assert(parent->deleted && child->deleted);
        assert(host_node(&view,10)->object != parent && host_node(&view,11)->object != child);
        assert(host_node(&view,12)->object == sibling && !sibling->deleted);
        parent = host_node(&view,10)->object;
        assert(parent->props[88] == 0 && parent->props[28] == 0 && parent->props[29] == 0);
        assert(parent->props[FW_UI_STYLE_RADIUS] == 0 && parent->props[FW_UI_STYLE_BORDER_COLOR] == 0);
        assert(parent->props[FW_UI_STYLE_BORDER_WIDTH] == 0 && parent->props[FW_UI_STYLE_OPA] == 0);
        clear_counts(); assert(styled_section_page(&defaults,&inherited,"Heading") == 0);
        assert(mutations() == 0);
    }
}
static void test_section_heading_allocation_is_atomic(void)
{
    const struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    const struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct widget *parent, *child, *heading;
    uint32_t generation;
    reset(); assert(styled_section_page(&style,&visual,"") == 0);
    parent = host_node(&view,10)->object; child = host_node(&view,11)->object;
    generation = context.committed.generation;
    fail_next_create = 1;
    clear_counts(); assert(styled_section_page(&style,&visual,"New title") == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && view.render_valid && context.committed.generation == generation);
    assert(host_node(&view,10)->primary == NULL && child->y == 0);
    assert(styled_section_page(&style,&visual,"New title") == 0);
    heading = host_node(&view,10)->primary;
    assert(host_node(&view,10)->object == parent && host_node(&view,11)->object == child);
    assert(heading != NULL && heading->parent == parent && !(heading->flags & 1u));
    assert(strcmp(heading->text,"New title") == 0 && heading->width == parent->width);
    assert(heading->x == 0 && heading->y == 0 && child->y == 40);
    clear_counts(); assert(styled_section_page(&style,&visual,"New title") == 0);
    assert(mutations() == 0);
    assert(styled_section_page(&style,&visual,"") == 0);
    assert(strcmp(heading->text,"") == 0);
    assert(child->y == 0);
    assert(styled_section_page(&style,&visual,"Restored") == 0);
    assert(host_node(&view,10)->primary == heading && strcmp(heading->text,"Restored") == 0);
}
static int stretched_section_page(uint8_t justify, int padded, int constrained)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_layout_v1 outer = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_layout_v1 inner = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_layout_v1 leaf = CANOPUS_UI_LAYOUT_DEFAULT;
    assert(CANOPUS_UI_SECTION(tree,10,"") == 0);
    outer.width = 180; outer.height = 200;
    outer.axis = CANOPUS_UI_AXIS_HORIZONTAL; outer.align = CANOPUS_UI_ALIGN_STRETCH;
    if (padded) { outer.padding_top = 10; outer.padding_bottom = 15; }
    assert(canopus_ui_node_set_layout(tree,10,&outer) == 0);
    assert(CANOPUS_UI_SECTION(tree,11,"") == 0);
    inner.width = 180; inner.justify = justify;
    if (padded) { inner.margin_top = 5; inner.margin_bottom = 10; inner.padding_top = 4; inner.padding_bottom = 6; }
    if (constrained) inner.max_height = 100;
    assert(canopus_ui_node_set_layout(tree,11,&inner) == 0);
    assert(CANOPUS_UI_SECTION(tree,12,"") == 0);
    row(tree,13,"Bottom",401); leaf.width = 180; leaf.height = 20;
    assert(canopus_ui_node_set_layout(tree,13,&leaf) == 0);
    assert(canopus_ui_end(tree) == 0);
    assert(canopus_ui_end(tree) == 0);
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_stretch_relayouts_descendants(void)
{
    reset(); assert(stretched_section_page(CANOPUS_UI_JUSTIFY_END,0,0) == 0);
    assert(host_node(&view,11)->height == 200 && host_node(&view,12)->y == 180);
    assert(((struct widget *)host_node(&view,12)->object)->y == 180);
    assert(host_node(&view,13)->y == 0);
    clear_counts(); assert(stretched_section_page(CANOPUS_UI_JUSTIFY_END,0,0) == 0);
    assert(mutations() == 0);
    assert(stretched_section_page(CANOPUS_UI_JUSTIFY_CENTER,0,0) == 0);
    assert(host_node(&view,12)->y == 90);
    assert(stretched_section_page(CANOPUS_UI_JUSTIFY_END,1,0) == 0);
    assert(host_node(&view,11)->height == 160 && host_node(&view,11)->y == 15);
    assert(host_node(&view,12)->y == 134);
    assert(stretched_section_page(CANOPUS_UI_JUSTIFY_END,1,1) == 0);
    assert(host_node(&view,11)->height == 100 && host_node(&view,12)->y == 74);
    clear_counts(); assert(stretched_section_page(CANOPUS_UI_JUSTIFY_END,1,1) == 0);
    assert(mutations() == 0);
}
static void test_nested_stretch_constraints(void)
{
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    unsigned depth;
    reset(); tree = begin();
    for (depth = 0; depth < 6; depth++) {
        assert(CANOPUS_UI_SECTION(tree,10+depth,"") == 0);
        layout.width = 180; layout.height = depth == 0 ? 200 : -1;
        layout.axis = CANOPUS_UI_AXIS_HORIZONTAL; layout.align = CANOPUS_UI_ALIGN_STRETCH;
        assert(canopus_ui_node_set_layout(tree,10+depth,&layout) == 0);
    }
    assert(CANOPUS_UI_SECTION(tree,20,"") == 0);
    layout.axis = CANOPUS_UI_AXIS_VERTICAL; layout.justify = CANOPUS_UI_JUSTIFY_END;
    assert(canopus_ui_node_set_layout(tree,20,&layout) == 0);
    row(tree,21,"Bottom",401); layout.height = 20;
    assert(canopus_ui_node_set_layout(tree,21,&layout) == 0);
    assert(canopus_ui_end(tree) == 0);
    for (depth = 0; depth < 6; depth++) assert(canopus_ui_end(tree) == 0);
    assert(finish(tree) == 0);
    for (depth = 0; depth < 6; depth++) assert(host_node(&view,10+depth)->height == 200);
    assert(host_node(&view,20)->height == 200 && host_node(&view,21)->y == 180);
}
static int padded_row_page(uint16_t type, const struct canopus_ui_layout_v1 *layout)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_component_props_v1 props = {
        sizeof(props),"Check",5,"Details",7,401,
        CANOPUS_UI_NODE_FLAG_VISIBLE | CANOPUS_UI_NODE_FLAG_ENABLED,0,0,0,0,0
    };
    if (type == CANOPUS_UI_NODE_STATUS_ROW) assert(CANOPUS_UI_STATUS_ROW(tree,10,"Check","Details") == 0);
    else if (type == CANOPUS_UI_NODE_BUTTON) assert(CANOPUS_UI_BUTTON(tree,10,"Check",401,1) == 0);
    else if (type == CANOPUS_UI_NODE_ACTION_ROW) row(tree,10,"Check",401);
    else if (type == CANOPUS_UI_NODE_SWITCH_ROW) assert(CANOPUS_UI_SWITCH_ROW(tree,10,"Check","Details",401,0,1) == 0);
    else assert(canopus_ui_component(tree,10,type,&props) == 0);
    assert(canopus_ui_node_set_layout(tree,10,layout) == 0);
    return finish(tree);
}
static int mark_variant_page(uint16_t variant)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_component_props_v1 props = {
        sizeof(props),"Radio",5,"",0,401,
        CANOPUS_UI_NODE_FLAG_VISIBLE | CANOPUS_UI_NODE_FLAG_ENABLED |
            CANOPUS_UI_NODE_FLAG_SELECTED,0,0,0,0,0
    };
    layout.width = 180; layout.height = 96;
    assert(canopus_ui_component(tree,10,CANOPUS_UI_NODE_RADIO_ROW,&props) == 0);
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    style.variant = variant;
    assert(canopus_ui_node_set_style(tree,10,&style) == 0);
    return finish(tree);
}
/* VARIANT_PLAIN hands the selection mark to the caller: the firmware glyph is
 * hidden but its column stays reserved, so a page can paint its own icon in the
 * same place without reflowing the label. */
static void test_plain_variant_hides_mark_and_keeps_column(void)
{
    struct widget *primary, *indicator;
    uint32_t generation;
    reset(); assert(mark_variant_page(CANOPUS_UI_VARIANT_DEFAULT) == 0);
    primary = host_node(&view,10)->primary;
    indicator = host_node(&view,10)->indicator;
    assert(primary->x == 0 && primary->width == 136);
    assert(indicator->x == 136 && !(indicator->flags & 1u));
    assert(!strcmp(indicator->text,"(\u25cf)"));
    generation = context.committed.generation;
    assert(mark_variant_page(CANOPUS_UI_VARIANT_PLAIN) == 0);
    assert(context.committed.generation != generation);
    assert(!((struct widget *)host_node(&view,10)->object)->deleted);
    assert(primary->width == 136);
    assert(indicator->x == 136 && (indicator->flags & 1u));
    assert(mark_variant_page(CANOPUS_UI_VARIANT_DEFAULT) == 0);
    assert(primary->width == 136 && indicator->x == 136 && !(indicator->flags & 1u));
}

static void test_generic_row_padding_and_stock_rejection(void)
{
    const uint16_t types[] = {CANOPUS_UI_NODE_CHECKBOX,CANOPUS_UI_NODE_RADIO_ROW,
        CANOPUS_UI_NODE_STATUS_ROW,CANOPUS_UI_NODE_BUTTON,CANOPUS_UI_NODE_ACTION_ROW,CANOPUS_UI_NODE_SWITCH_ROW};
    unsigned i, side;
    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
        struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
        struct widget *object;
        uint32_t generation;
        reset(); layout.width = 180; layout.height = 96;
        assert(padded_row_page(types[i],&layout) == 0);
        object = host_node(&view,10)->object; generation = context.committed.generation;
        if (i < 2) {
            struct widget *primary = host_node(&view,10)->primary;
            struct widget *secondary = host_node(&view,10)->secondary;
            struct widget *indicator = host_node(&view,10)->indicator;
            /* The synthesized mark trails the text: the primary keeps the
               leading edge and the fixed 44px column sits flush right. */
            assert(primary->x == 0 && primary->width == 136);
            assert(indicator->x == 136 && !(indicator->flags & 1u));
            layout.padding_left = 20; layout.padding_right = 6; layout.padding_top = 9; layout.padding_bottom = 3;
            assert(padded_row_page(types[i],&layout) == 0);
            assert(host_node(&view,10)->object == object && object->width == 180 && object->height == 96);
            assert(primary->x == 20 && primary->y == 9 && primary->width == 110);
            assert(secondary->width == 110 && indicator->x == 130 && indicator->y == 9);
            clear_counts(); assert(padded_row_page(types[i],&layout) == 0); assert(mutations() == 0);
            layout.padding_left = layout.padding_right = layout.padding_top = layout.padding_bottom = 0;
            assert(padded_row_page(types[i],&layout) == 0);
            assert(primary->x == 0 && primary->y == 0 && primary->width == 136);
            assert(indicator->x == 136);
        } else for (side = 0; side < 4; side++) {
            struct canopus_ui_layout_v1 padded = layout;
            if (side == 0) padded.padding_left = 20;
            else if (side == 1) padded.padding_top = 20;
            else if (side == 2) padded.padding_right = 20;
            else padded.padding_bottom = 20;
            clear_counts(); assert(padded_row_page(types[i],&padded) == CANOPUS_UI_ERR_UNSUPPORTED);
            assert(mutations() == 0 && context.committed.generation == generation);
            assert(host_node(&view,10)->object == object && !object->deleted && view.render_valid);
        }
    }
}
static int styled_page_viewport(int custom)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    unsigned i;
    if (custom) { style.foreground = CANOPUS_UI_COLOR_DANGER; style.background = CANOPUS_UI_COLOR_SURFACE_ALT; style.corner_radius = 8; }
    assert(canopus_ui_node_set_style(tree,1,&style) == 0);
    for (i = 0; i < 6; i++) row(tree,10+i,"Viewport item",401+i);
    return finish(tree);
}
static void test_page_style_reset_preserves_scroll_and_capture(void)
{
    struct widget *viewport, *item;
    struct canopus_ui_host_binding binding;
    struct host_input_state *state;
    uint32_t generation;
    reset(); assert(styled_page_viewport(1) == 0);
    viewport = view.content_root; item = host_node(&view,10)->object;
    scroll_to_y(viewport,60,0); assert(viewport->scroll_y == 60);
    binding = host_node(&view,10)->binding; capture_on_press = 1;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
    state = host_input_state(&view); generation = context.committed.generation;
    fail_next_create = 1; clear_counts();
    assert(styled_page_viewport(0) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && view.render_valid && view.content_root == viewport);
    assert(view.rendered_generation == generation && context.committed.generation == generation);
    assert(state->pressed && state->captured && event_calls == 1 && !(viewport->flags & 16u));
    /* Fail after a candidate viewport exists, without publishing it. */
    fail_create_at = allocated + 1;
    assert(styled_page_viewport(0) == CANOPUS_UI_ERR_BACKEND);
    assert(view.content_root == viewport && !viewport->deleted && !item->deleted);
    assert(view.render_valid && state->pressed && state->captured && event_calls == 1);
    assert(styled_page_viewport(0) == 0);
    assert(viewport->deleted && item->deleted && view.content_root != viewport);
    assert(((struct widget *)view.content_root)->scroll_y == 60);
    assert(((struct widget *)view.content_root)->props[88] == 0 && ((struct widget *)view.content_root)->props[28] == 0);
    assert(state->pressed && state->captured && event_calls == 1);
    assert(!(((struct widget *)view.content_root)->flags & 16u));
    binding = host_node(&view,10)->binding;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_MOVE,110,9,12) == 0);
    assert(last_input.key == 10 && last_input.delta_x == 4 && last_input.delta_y == 4);
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_RELEASE,120,9,12) == 0);
    assert(((struct widget *)view.content_root)->flags & 16u);
    clear_counts(); assert(styled_page_viewport(0) == 0); assert(mutations() == 0);
}
static int gesture_candidate_page(unsigned proposal)
{
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_component_props_v1 scroll = {
        sizeof(scroll),"",0,"",0,701,CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,0
    };
    struct canopus_ui_action_row_props_v1 action = {sizeof(action),"Dragged",7,"",0,702,proposal != 7};
    struct canopus_ui_text_props_v1 text = {sizeof(text),"Saved",5,CANOPUS_UI_TEXT_BODY};
    assert(canopus_ui_tree_begin(&context,&tree) == 0);
    assert(CANOPUS_UI_NAVIGATION_PAGE(tree,proposal == 1 || proposal == 10 ? 3 : 1,"Manager") == 0);
    if (proposal == 8) scroll.flags = 0;
    assert(canopus_ui_component(tree,10,CANOPUS_UI_NODE_SCROLL,&scroll) == 0);
    layout.height = 100; visual.flags = CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT; visual.content_height = 900;
    if (proposal == 2) style.background = CANOPUS_UI_COLOR_DANGER;
    else if (proposal == 6) { visual.flags = 0; visual.content_height = 0; }
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    assert(canopus_ui_node_set_style(tree,10,&style) == 0);
    assert(canopus_ui_action_row(tree,proposal >= 1 && proposal <= 5 ? 21 : 20,&action) == 0);
    assert(canopus_ui_end(tree) == 0);
    if (proposal == 3) assert(CANOPUS_UI_SECTION(tree,30,"New heading") == 0);
    else assert(CANOPUS_UI_SECTION(tree,30,"") == 0);
    assert(canopus_ui_end(tree) == 0);
    if (proposal == 4) {
        struct canopus_ui_navigation_header_props_v1 header = {
            sizeof(header),"Next route",10,"",0,5,1,0,0
        };
        assert(canopus_ui_navigation_header(tree,2,&header) == 0);
        assert(canopus_ui_end(tree) == 0);
    } else if (proposal == 5) assert(canopus_ui_text(tree,15,&text) == 0);
    if (proposal == 1) row(tree,40,"Candidate",703);
#ifndef CANOPUS_TEST_NO_FONT
    if (proposal == 9 || proposal == 10) font_text(tree,50,proposal == 9 ? 21 : 26,1);
#endif
    return finish(tree);
}
#ifndef CANOPUS_TEST_NO_FONT
static void test_font_cancel_destroying_root(void)
{
    unsigned ordering;
    for (ordering = 0; ordering < 2; ordering++) {
        struct canopus_ui_host_binding binding;
        reset(); assert(gesture_candidate_page(9) == 0);
        assert(native_fonts == 1);
        binding = host_node(&view,20)->binding; capture_on_press = 1;
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
        detach_on_cancel = destroy_root_on_cancel = 1;
        destroy_root_before_detach = ordering;
        assert(gesture_candidate_page(10) == CANOPUS_UI_ERR_BACKEND);
        assert(!host_view_alive(&view) && native_fonts == 0 && font_bindings_allocated == 0);
        assert(s_fonts == NULL && s_font_labels == NULL);
    }
}
#endif
static void test_failed_candidates_preserve_captured_gesture(void)
{
    const unsigned old_proposals[] = {0,2,0,0,0,6};
    const unsigned proposals[] = {1,1,3,4,5,7};
    unsigned scenario;
    for (scenario = 0; scenario < sizeof(proposals)/sizeof(proposals[0]); scenario++) {
        struct canopus_ui_host_binding binding;
        struct host_input_state *state;
        struct widget *parent, *item, *title;
        uint32_t generation;
        unsigned before;
        reset(); assert(gesture_candidate_page(old_proposals[scenario]) == 0);
        parent = host_node(&view,10)->object;
        /* Cases with the alternate style or absent extent still press key20. */
        binding = host_node(&view,old_proposals[scenario] == 2 ? 21 : 20)->binding;
        item = host_node(&view,binding.key)->object; title = view.page_title;
        capture_on_press = 1;
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
        state = host_input_state(&view); generation = context.committed.generation; before = allocated;
        assert(state->pressed && state->captured);
        if (scenario == 2 || scenario == 4) fail_create_at = allocated + 1;
        else if (scenario == 3) fail_create_at = allocated + 1;
        else fail_next_create = 1;
        clear_counts();
        assert(gesture_candidate_page(proposals[scenario]) == CANOPUS_UI_ERR_BACKEND);
        assert(view.render_valid && view.rendered_generation == generation && context.committed.generation == generation);
        assert(host_node(&view,binding.key)->object == item && !item->deleted && !parent->deleted);
        assert(view.page_title == title && !title->deleted);
        assert(state->pressed && state->captured && event_calls == 1);
        assert(!(parent->flags & 16u));
        while (before < allocated) assert(widgets[before++].deleted);
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_MOVE,110,9,12) == 0);
        assert(last_input.type == CANOPUS_UI_EVENT_MOVE && last_input.key == binding.key);
        assert(last_input.delta_x == 4 && last_input.delta_y == 4);
        if (scenario == 0) {
            assert(gesture_candidate_page(proposals[scenario]) == 0);
            assert(event_calls == 3 && last_input.type == CANOPUS_UI_EVENT_CANCEL);
            assert(last_input.generation == generation && !state->pressed && !state->captured);
        } else {
            assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_RELEASE,120,9,12) == 0);
            assert(last_input.type == CANOPUS_UI_EVENT_RELEASE && !state->pressed && !state->captured);
        }
    }
}
static void test_host_capture_invalidation_and_detach(void)
{
    const unsigned proposals[] = {7,8,1};
    unsigned scenario;
    for (scenario = 0; scenario < sizeof(proposals)/sizeof(proposals[0]); scenario++) {
        struct canopus_ui_host_binding binding;
        struct host_input_state *state;
        struct widget *parent, *item, *root;
        uint32_t generation;
        unsigned before;
        reset(); assert(gesture_candidate_page(0) == 0);
        root = view.root;
        parent = host_node(&view,10)->object; item = host_node(&view,20)->object;
        binding = host_node(&view,20)->binding; capture_on_press = 1;
        assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
        state = host_input_state(&view); generation = context.committed.generation; before = allocated;
        if (scenario == 2) detach_on_cancel = 1;
        assert(gesture_candidate_page(proposals[scenario]) == (scenario == 2 ? CANOPUS_UI_ERR_BACKEND : CANOPUS_UI_OK));
        assert(event_calls == 2 && last_input.type == CANOPUS_UI_EVENT_CANCEL);
        assert(last_input.generation == generation && !state->pressed && !state->captured);
        assert(parent->flags & 16u);
        if (scenario == 2) {
            unsigned staged = before;
            assert(!host_view_alive(&view) && context.committed.generation == generation);
            assert(!parent->deleted && !item->deleted);
            /* Staged candidates belong to the native parent once the view is
             * gone; firmware reclaims them with the page subtree. */
            while (staged < allocated) assert(!widgets[staged++].deleted);
            delete_object(root);
            assert(parent->deleted && item->deleted);
            for (staged = 0; staged < allocated; staged++) assert(widgets[staged].deleted);
        } else {
            binding = host_node(&view,20)->binding;
            assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_RELEASE,120,9,12) == CANOPUS_UI_ERR_STALE);
            clear_counts(); assert(gesture_candidate_page(proposals[scenario]) == 0);
            assert(event_calls == 2 && mutations() == 0);
        }
    }
}
/* Native delete reaps descendants. A removed parent can occupy a higher slot
 * than its removed child when re-parenting reuses the child's slot, and the
 * dialog commit's early return leaves those slots outside the previous
 * snapshot, so leftover cleanup alone must still delete each object once. */
static int inverted_subtree_page(unsigned proposal)
{
    struct canopus_ui_tree_v1 *tree = begin();
    if (proposal == 0u) {
        row(tree, 20, "A", 401);
    } else if (proposal == 1u) {
        assert(CANOPUS_UI_SECTION(tree, 21, "B") == 0);
        row(tree, 20, "A", 401);
        assert(canopus_ui_end(tree) == 0);
    }
    return finish(tree);
}
static void test_removed_subtree_with_inverted_slots_deletes_once(void)
{
    struct widget *a, *b;
    reset(); assert(inverted_subtree_page(0) == 0);
    assert(inverted_subtree_page(1) == 0);
    a = host_node(&view,20)->object; b = host_node(&view,21)->object;
    assert(a->parent == b);
    /* The parent's slot index is higher than the child's: the inverted case. */
    assert(host_node(&view,21) - view.nodes > host_node(&view,20) - view.nodes);
    assert(dialog_page("Delete resource?",301) == 0);
    assert(inverted_subtree_page(2) == 0);
    assert(a->deleted && b->deleted);
}
static void test_cancel_destroying_root_reclaims_staged_candidates(void)
{
    struct canopus_ui_host_binding binding;
    uint32_t generation;
    unsigned before, i;
    /* A CANCEL that closes the page destroys the native root exactly like
     * firmware: the root reclaims every staged candidate and the host must
     * not delete any of them again. */
    reset(); assert(gesture_candidate_page(0) == 0);
    binding = host_node(&view,20)->binding; capture_on_press = 1;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
    assert(host_input_state(&view)->pressed && host_input_state(&view)->captured);
    generation = context.committed.generation; before = allocated;
    detach_on_cancel = destroy_root_on_cancel = 1;
    assert(gesture_candidate_page(1) == CANOPUS_UI_ERR_BACKEND);
    assert(last_input.type == CANOPUS_UI_EVENT_CANCEL && event_calls == 2);
    assert(last_input.generation == generation && !host_view_alive(&view));
    assert(context.committed.generation == generation);
    for (i = before; i < allocated; i++) assert(widgets[i].deleted);
    /* The dialog path stages its box under the same root. Close order is
     * destroy-then-detach here: the page root dies first, so neither the
     * failed commit nor detach may delete the staged box again. */
    reset(); assert(gesture_candidate_page(0) == 0);
    binding = host_node(&view,20)->binding; capture_on_press = 1;
    assert(host_input_route(&view,&binding,CANOPUS_UI_EVENT_PRESS,100,5,8) == 0);
    generation = context.committed.generation; before = allocated;
    detach_on_cancel = destroy_root_on_cancel = destroy_root_before_detach = 1;
    assert(dialog_page("Delete resource?",301) == CANOPUS_UI_ERR_BACKEND);
    assert(last_input.type == CANOPUS_UI_EVENT_CANCEL && event_calls == 2);
    assert(!host_view_alive(&view) && context.committed.generation == generation);
    for (i = before; i < allocated; i++) assert(widgets[i].deleted);
}
static int image_fit_page(uint16_t fit, int width, int height,
                          const char *path, uint32_t resource_id)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_component_props_v1 image = {
        sizeof(image),path,(uint32_t)strlen(path),"",0,0,
        CANOPUS_UI_NODE_FLAG_VISIBLE,0,0,0,0,resource_id
    };
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    int32_t result;
    assert(canopus_ui_component(tree,10,CANOPUS_UI_NODE_IMAGE,&image) == 0);
    layout.width = (int16_t)width; layout.height = (int16_t)height;
    assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
    visual.image_fit = fit;
    result = canopus_ui_node_set_visual(tree,10,&visual);
    if (result != 0) { canopus_ui_tree_abort(tree); return result; }
    return finish(tree);
}
static void test_image_fit_contract(void)
{
    struct widget *image;
#ifdef CANOPUS_TEST_NO_IMAGE_FIT
    uint32_t generation;
    uint16_t fit;
#endif
    reset(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_NATIVE,100,80,"/image.bin",1) == 0);
    image = host_node(&view,10)->object;
    assert(image->width == 100 && image->height == 80 && strcmp(image->source,"/image.bin") == 0);
#ifdef CANOPUS_TEST_NO_IMAGE_FIT
    generation = context.committed.generation;
    for (fit = CANOPUS_UI_IMAGE_FIT_CONTAIN; fit <= CANOPUS_UI_IMAGE_FIT_STRETCH; fit++) {
        clear_counts();
        assert(image_fit_page(fit,100,80,"/image.bin",1) == CANOPUS_UI_ERR_UNSUPPORTED);
        assert(mutations() == 0 && context.committed.generation == generation);
        assert(host_node(&view,10)->object == image && !image->deleted);
    }
#else
    {
        struct widget *viewport;
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,100,80,"/image.bin",1) == 0);
        assert(image->deleted);
        viewport = host_node(&view,10)->object;
        image = host_node(&view,10)->secondary;
        assert(image->parent == viewport && viewport->width == 100 && viewport->height == 80);
        assert(!(viewport->flags & (16u | 0x100000u)));
        assert(viewport->props[16] == 0 && viewport->props[17] == 0 && viewport->props[18] == 0 && viewport->props[19] == 0);
        assert(viewport->props[29] == 0 && viewport->props[48] == 0 && viewport->props[12] == 0);
        assert(image->width == 100 && image->height == 80 && image->x == 0 && image->y == 0);
        assert(calls.source == 1 && calls.scale == 1 && calls.info == 1);
        assert(image->scale_x == 128 && image->scale_y == 128);
        /* Native mode 9 centers the scaled source: 100x50 inside 100x80. */
        assert(image->source_width * image->scale_x == 100u * 256u);
        assert((80u * 256u - image->source_height * image->scale_y) / 512u == 15u);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,100,80,"/image.bin",1) == 0);
        assert(mutations() == 0 && calls.info == 0);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_COVER,100,80,"/image.bin",1) == 0);
        assert(calls.scale == 1 && calls.source == 0 && calls.create == 0);
        assert(image->scale_x == 205 && image->scale_y == 205);
        assert(image->source_width * image->scale_x > 100u * 256u);
        assert(image->source_height * image->scale_y >= 80u * 256u);
        assert(!(viewport->flags & 0x100000u)); /* Enlarged source is clipped by its parent. */
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_STRETCH,100,80,"/image.bin",1) == 0);
        assert(calls.scale == 1 && calls.source == 0 && calls.create == 0);
        assert(image->scale_x == 128 && image->scale_y == 205);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_STRETCH,50,25,"/image.bin",1) == 0);
        assert(calls.scale == 1 && calls.source == 0 && image->scale_x == 64 && image->scale_y == 64);
        image_width = 100; image_height = 200;
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_STRETCH,50,25,"/image.bin",2) == 0);
        assert(calls.source == 1 && calls.scale == 1 && image->scale_x == 128 && image->scale_y == 32);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,50,25,"/other.bin",3) == 0);
        assert(calls.source == 1 && calls.scale == 1 && image->scale_x == 32 && image->scale_y == 32);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_NATIVE,50,25,"/other.bin",3) == 0);
        assert(viewport->deleted && image->deleted);
        image = host_node(&view,10)->object;
        assert(host_node(&view,10)->secondary == NULL && image->scale_x == 256 && image->scale_y == 256);
        clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_NATIVE,50,25,"/other.bin",3) == 0);
        assert(mutations() == 0);
    }
#endif
}
#ifndef CANOPUS_TEST_NO_IMAGE_FIT
static int image_fit_subtree_page(uint16_t fit, uint16_t foreground,
                                  uint32_t parent_key, int visible, int omit)
{
    struct canopus_ui_tree_v1 *tree = begin();
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_component_props_v1 image = {
        sizeof(image),"/image.bin",10,"",0,0,
        visible ? CANOPUS_UI_NODE_FLAG_VISIBLE : 0,0,0,0,0,1
    };
    assert(CANOPUS_UI_SECTION(tree,parent_key,"") == 0);
    style.foreground = foreground;
    assert(canopus_ui_node_set_style(tree,parent_key,&style) == 0);
    if (!omit) {
        assert(canopus_ui_component(tree,10,CANOPUS_UI_NODE_IMAGE,&image) == 0);
        layout.width = 100; layout.height = 80; visual.image_fit = fit;
        assert(canopus_ui_node_set_layout(tree,10,&layout) == 0);
        assert(canopus_ui_node_set_visual(tree,10,&visual) == 0);
    }
    assert(canopus_ui_end(tree) == 0);
    return finish(tree);
}
static void test_image_fit_rebuild_and_lifetime(void)
{
    struct widget *viewport, *image, *parent;
    uint32_t generation;
    unsigned before;
    reset(); assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_DANGER,20,1,0) == 0);
    parent = host_node(&view,20)->object;
    viewport = host_node(&view,10)->object; image = host_node(&view,10)->secondary;
    /* Removing a local ancestor style rebuilds every descendant. The new image
     * needs scaling even though its own model bytes have not changed. */
    clear_counts(); assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,20,1,0) == 0);
    assert(parent->deleted && viewport->deleted && image->deleted && calls.scale == 1);
    viewport = host_node(&view,10)->object; image = host_node(&view,10)->secondary;
    assert(image->parent == viewport && image->scale_x == 205 && image->scale_y == 205);
    clear_counts(); assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,20,0,0) == 0);
    assert(viewport->flags & 1u); assert(calls.scale == 0 && calls.source == 0);
    clear_counts(); assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,20,1,0) == 0);
    assert(!(viewport->flags & 1u) && calls.scale == 0 && calls.source == 0);
    generation = context.committed.generation; before = allocated;
    /* Reparenting stages parent + viewport + image. Fail the last allocation. */
    fail_create_at = before + 2;
    assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,21,1,0) == CANOPUS_UI_ERR_BACKEND);
    assert(context.committed.generation == generation && !viewport->deleted && !image->deleted);
    assert(widgets[before].deleted && widgets[before + 1].deleted);
    clear_counts(); assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,21,1,0) == 0);
    assert(viewport->deleted && image->deleted && calls.scale == 1);
    viewport = host_node(&view,10)->object; image = host_node(&view,10)->secondary;
    assert(image->scale_x == 205 && image->scale_y == 205);
    assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_COVER,CANOPUS_UI_COLOR_INHERIT,21,1,1) == 0);
    assert(viewport->deleted && image->deleted && host_node(&view,10) == NULL);
    assert(image_fit_subtree_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,CANOPUS_UI_COLOR_INHERIT,21,1,0) == 0);
    image = host_node(&view,10)->secondary;
    assert(image->scale_x == 128 && image->scale_y == 128);
}
static void test_image_fit_across_native_dialog(void)
{
    unsigned from, to;
    for (from = 0; from < 2; from++) for (to = 0; to < 2; to++) {
        uint16_t old_fit = from ? CANOPUS_UI_IMAGE_FIT_COVER : CANOPUS_UI_IMAGE_FIT_NATIVE;
        uint16_t new_fit = to ? CANOPUS_UI_IMAGE_FIT_COVER : CANOPUS_UI_IMAGE_FIT_NATIVE;
        struct widget *old_object, *old_image, *object, *image;
        reset(); assert(image_fit_page(old_fit,100,80,"/image.bin",1) == 0);
        old_object = host_node(&view,10)->object;
        old_image = from ? host_node(&view,10)->secondary : old_object;
        assert(dialog_page("Confirm",301) == 0);
        /* Native dialogs retain the live body while committing a different
         * snapshot, including a button that reuses the image's key. */
        assert(host_node(&view,10)->object == old_object && !old_image->deleted);
        clear_counts(); assert(image_fit_page(new_fit,100,80,"/image.bin",1) == 0);
        object = host_node(&view,10)->object;
        image = to ? host_node(&view,10)->secondary : object;
        assert(image != NULL && strcmp(image->source,"/image.bin") == 0);
        assert((host_node(&view,10)->secondary != NULL) == (to != 0));
        if (from != to) assert(old_object->deleted && old_image->deleted && object != old_object);
        else assert(object == old_object && image == old_image && !old_object->deleted);
        assert(image->scale_x == (to ? 205u : 256u) && image->scale_y == (to ? 205u : 256u));
        assert(calls.source == 1 && calls.scale == to);
        clear_counts(); assert(image_fit_page(new_fit,100,80,"/image.bin",1) == 0);
        assert(mutations() == 0);
    }
}
static void test_image_fit_quantization(void)
{
    struct widget *image;
    reset(); image_width = 300; image_height = 200;
    assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,100,80,"/fraction.bin",1) == 0);
    image = host_node(&view,10)->secondary;
    assert(image->scale_x == 85 && image->scale_y == 85);
    assert(image->source_width * image->scale_x <= 100u * 256u);
    assert(image->source_height * image->scale_y <= 80u * 256u);
    assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_COVER,100,80,"/fraction.bin",1) == 0);
    assert(image->scale_x == 103 && image->scale_y == 103);
    assert(image->source_width * image->scale_x >= 100u * 256u);
    assert(image->source_height * image->scale_y >= 80u * 256u);
    assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_STRETCH,100,80,"/fraction.bin",1) == 0);
    assert(image->scale_x == 86 && image->scale_y == 103);
    /* Ceil matches native stretch. Any sub-Q8 overshoot is cropped by the
     * viewport; no integer scale can express these exact nonintegral ratios. */
    assert(image->source_width * image->scale_x >= 100u * 256u);
    assert(image->source_height * image->scale_y >= 80u * 256u);
}
static void test_image_fit_preflight(void)
{
    struct widget *viewport, *image;
    uint32_t generation;
    unsigned allocated_before;
    reset(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_COVER,100,80,"/image.bin",1) == 0);
    viewport = host_node(&view,10)->object; image = host_node(&view,10)->secondary;
    generation = context.committed.generation; allocated_before = allocated;
    image_decode_failure = 1;
    clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_COVER,100,80,"/missing.bin",2) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && allocated == allocated_before && context.committed.generation == generation);
    assert(!viewport->deleted && !image->deleted && strcmp(image->source,"/image.bin") == 0);
    image_decode_failure = 0; image_width = 0;
    clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_COVER,100,80,"/empty.bin",2) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && context.committed.generation == generation);
    image_width = image_height = 1;
    clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_STRETCH,32767,32767,"/huge.bin",2) == CANOPUS_UI_ERR_UNSUPPORTED);
    assert(mutations() == 0 && context.committed.generation == generation);
    image_width = image_height = UINT16_MAX;
    clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_CONTAIN,1,1,"/tiny.bin",2) == CANOPUS_UI_ERR_UNSUPPORTED);
    assert(mutations() == 0 && context.committed.generation == generation);
    image_width = 200; image_height = 100;
    fail_next_create = 1;
    clear_counts(); assert(image_fit_page(CANOPUS_UI_IMAGE_FIT_NATIVE,100,80,"/image.bin",2) == CANOPUS_UI_ERR_BACKEND);
    assert(mutations() == 0 && !viewport->deleted && !image->deleted);
    assert(context.committed.generation == generation);
}
#endif
static void test_advertised_capabilities(void)
{
    uint32_t caps;
    reset();
    caps = canopus_ui_context_capabilities(&context);
#ifdef CANOPUS_TEST_NO_IMAGE_FIT
    assert((caps & (CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN | CANOPUS_UI_CAP_IMAGE_FIT_COVER | CANOPUS_UI_CAP_IMAGE_FIT_STRETCH)) == 0);
#else
    assert((caps & (CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN | CANOPUS_UI_CAP_IMAGE_FIT_COVER | CANOPUS_UI_CAP_IMAGE_FIT_STRETCH)) ==
           (CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN | CANOPUS_UI_CAP_IMAGE_FIT_COVER | CANOPUS_UI_CAP_IMAGE_FIT_STRETCH));
#endif
    /* Virtual content uses the same native container primitives as layout. */
    assert((caps & CANOPUS_UI_CAP_VIRTUAL_CONTENT) != 0);
    assert((caps & (CANOPUS_UI_CAP_STYLE | CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VALUES |
                    CANOPUS_UI_CAP_VISUAL)) ==
           (CANOPUS_UI_CAP_STYLE | CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VALUES |
            CANOPUS_UI_CAP_VISUAL));
    assert((caps & (1u << 16)) == 0); /* Retired capability bit stays unused. */
}
int main(int argc, char **argv)
{
    int baseline = argc > 1 && strcmp(argv[1], "--baseline") == 0;
    (void)host_input_route;
    test_advertised_capabilities(); test_image_fit_contract();
#ifndef CANOPUS_TEST_NO_IMAGE_FIT
    test_image_fit_across_native_dialog();
    test_image_fit_preflight(); test_image_fit_quantization(); test_image_fit_rebuild_and_lifetime();
#endif
    test_identical_and_binding(baseline);
    if (baseline) return 0;
    test_property_updates(); test_clip_toggle(); test_reorder_and_reappearance(); test_failure_retry();
    test_switch_events(); test_dialog_binding(); test_dialog_allocation_failure_preserves_state();
#ifdef CANOPUS_MGR_BAND11
    test_row_content_hits();
#endif
    test_nested_header_does_not_displace_rows(); test_header_back_binding();
    test_page_scroll_and_absolute_overflow();
    test_virtual_native_bounds_and_capture_replacement();
    test_identical_commit_preserves_capture_without_mutation();
    test_virtual_window_placement_while_scrolled();
    test_header_keeps_page_scroll_and_route_resets();
    test_reentrant_reused_keys_delete_old_rows();
    test_first_header_drives_shared_title();
    test_label_style_preserves_bounds(); test_font_size_contract();
#ifndef CANOPUS_TEST_NO_FONT
    test_font_cache_and_visibility(); test_font_dialog_retains_live_body();
    test_font_page_reload_and_view_reset(); test_font_preparation_failures();
    test_font_retirement_boundaries(); test_font_many_sizes_do_not_accumulate();
    test_font_surfaces_and_unsupported_nodes();
    test_font_cancel_destroying_root();
#endif
    test_inherited_style_rebuilds_subtree(); test_section_heading_allocation_is_atomic();
    test_stretch_relayouts_descendants(); test_nested_stretch_constraints();
    test_generic_row_padding_and_stock_rejection();
    test_plain_variant_hides_mark_and_keeps_column();
    test_page_style_reset_preserves_scroll_and_capture();
    test_failed_candidates_preserve_captured_gesture(); test_host_capture_invalidation_and_detach();
    test_cancel_destroying_root_reclaims_staged_candidates();
    test_removed_subtree_with_inverted_slots_deletes_once();
    reset();
    canopus_ui_host_detach(&view);
    puts("native UI backend regression tests passed");
    return 0;
}
