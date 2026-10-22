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
    unsigned events, destroy, dialog;
};
static struct counts calls;
struct widget {
    unsigned char native[128];
    unsigned flags, checked;
    int width, height, minimum, maximum, value;
    char text[256], secondary[256], source[256];
    void (*callback)(void *);
};
static struct widget widgets[256];
static unsigned allocated;
static int fail_next_create;

static void text_copy(char *out, const char *text)
{
    snprintf(out, 256, "%s", text != NULL ? text : "");
}
static void *new_widget(void *parent)
{
    (void)parent;
    if (fail_next_create) { fail_next_create = 0; return NULL; }
    assert(allocated < sizeof(widgets) / sizeof(widgets[0]));
    calls.create++;
    return &widgets[allocated++];
}
static void add_flag(void *object, uint32_t flag)
{ calls.flags++; ((struct widget *)object)->flags |= flag; }
static void clear_flag(void *object, uint32_t flag)
{ calls.flags++; ((struct widget *)object)->flags &= ~flag; }
static void set_size(void *object, int32_t width, int32_t height)
{
    calls.size++;
    ((struct widget *)object)->width = width;
    ((struct widget *)object)->height = height;
}
static void align_to(void *object, void *base, uint32_t align, int32_t x, int32_t y)
{ (void)object; (void)base; (void)align; (void)x; (void)y; calls.align++; }
#ifndef CANOPUS_MGR_BAND11
static void align_object(void *object, uint32_t align, int32_t x, int32_t y)
{ align_to(object, NULL, align, x, y); }
#endif
static void add_event(void *object, void (*callback)(void *), uint32_t code, void *user)
{
    (void)code; (void)user;
    calls.events++;
    ((struct widget *)object)->callback = callback;
}
static void set_text(void *object, const char *text)
{ calls.text++; text_copy(((struct widget *)object)->text, text); }
static void set_style(void *object, const void *style, uint32_t opacity, uint32_t selector)
{ (void)object; (void)style; (void)opacity; (void)selector; calls.style++; }
static void set_source(void *object, const void *source)
{ calls.source++; text_copy(((struct widget *)object)->source, source); }
static void set_range(void *object, int32_t minimum, int32_t maximum)
{
    calls.range++;
    ((struct widget *)object)->minimum = minimum;
    ((struct widget *)object)->maximum = maximum;
}
static void set_value(void *object, int32_t value, uint32_t animation)
{ (void)animation; calls.value++; ((struct widget *)object)->value = value; }
static void delete_object(void *object)
{ (void)object; calls.destroy++; }
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
#ifndef CANOPUS_MGR_BAND11
static void *create_title(void *root, const char *text, uint32_t mode,
                           void (*back)(void *), void *user)
{
    void *w = new_widget(root);
    (void)mode; (void)back; (void)user;
    if (w != NULL) text_copy(((struct widget *)w)->text, text);
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
#define FW_LVX_CONTENT_CREATE new_widget
#define FW_LVX_OBJECT_ALIGN align_object
#define FW_LVX_LIST_ROW_CREATE create_row
#define FW_LVX_LIST_ROW_TRAILING row_trailing
#define FW_LVX_LIST_ROW_UPDATE update_row
#define FW_LVX_EVENT_GET_CODE event_code
#define FW_LVX_PAGE_TITLE_CREATE create_title
#define FW_LVX_OBJECT_ADD_FLAG add_flag
#define FW_LVX_OBJECT_CLEAR_FLAG clear_flag
#define FW_LVX_OBJECT_DELETE delete_object
#define FW_LVX_OBJECT_SET_SIZE set_size
#define FW_LVX_ALIGN_TO align_to
#define FW_LVX_EVENT_ADD add_event
#define FW_LVX_LABEL_CREATE new_widget
#define FW_LVX_LABEL_SET_TEXT set_text
#define FW_LVX_IMAGE_CREATE new_widget
#define FW_LVX_IMAGE_SET_SRC set_source
#define FW_LVX_BAR_CREATE new_widget
#define FW_LVX_BAR_SET_RANGE set_range
#define FW_LVX_BAR_SET_VALUE set_value
#else
static void *create_title(void *root, const char *text, uint32_t mode, void (*back)(void *))
{
    void *w = new_widget(root);
    (void)mode; (void)back;
    if (w != NULL) text_copy(((struct widget *)w)->text, text);
    return w;
}
static void *add_content(void *object, const void *icon, const char *primary,
                          const char *secondary, int32_t value, uint8_t selected, uint8_t trailing)
{
    (void)icon; (void)value; (void)trailing;
    row_content(object, primary, secondary, selected);
    memcpy((unsigned char *)object + 0x54, &object, sizeof(object));
    return object;
}
static void update_row(void *object, const void *icon, const char *primary,
                        const char *secondary, int32_t value, uint8_t selected)
{
    (void)icon; (void)value; calls.row++; row_content(object, primary, secondary, selected);
}
static void remove_styles(void *object) { (void)object; calls.style++; }
static void set_padding(void *object, int32_t padding, uint32_t selector)
{ (void)object; (void)padding; (void)selector; calls.style++; }
static void set_width(void *object, int32_t width)
{ calls.size++; ((struct widget *)object)->width = width; }
static void set_long_mode(void *object, uint32_t mode)
{ (void)object; (void)mode; calls.style++; }
#define FW_LV_OBJECT_CREATE new_widget
#define FW_LV_OBJECT_REMOVE_STYLES remove_styles
#define FW_LV_OBJECT_PAD_TOP set_padding
#define FW_LV_OBJECT_PAD_BOTTOM set_padding
#define FW_LV_OBJECT_WIDTH set_width
#define FW_LV_LABEL_LONG_MODE set_long_mode
#define FW_LV_PAGE_TITLE_CREATE create_title
#define FW_LVX_LIST_ITEM_CREATE new_widget
#define FW_LVX_LIST_ITEM_ADD_CONTENT add_content
#define FW_LVX_LIST_ITEM_UPDATE update_row
#define FW_LVX_LIST_TRAILING_OFFSET 0x54u
#define FW_LV_OBJECT_ADD_FLAG add_flag
#define FW_LV_OBJECT_CLEAR_FLAG clear_flag
#define FW_LV_OBJECT_DELETE delete_object
#define FW_LV_OBJECT_SET_SIZE set_size
#define FW_LV_ALIGN_TO align_to
#define FW_LV_EVENT_ADD add_event
#define FW_LV_LABEL_CREATE new_widget
#define FW_LV_LABEL_SET_TEXT set_text
#define FW_LV_IMAGE_CREATE new_widget
#define FW_LV_IMAGE_SET_SRC set_source
#define FW_LV_BAR_CREATE new_widget
#define FW_LV_BAR_SET_RANGE set_range
#define FW_LV_BAR_SET_VALUE set_value
#endif
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
static int32_t receive_event(void *cookie, uint32_t generation, uint32_t key, uint32_t event)
{
    (void)cookie; assert(generation == context.committed.generation);
    event_calls++; last_key = key; last_event = event; return 0;
}
static void reset(void)
{
    canopus_ui_host_detach(&view);
    memset(widgets, 0, sizeof(widgets)); allocated = 0; fail_next_create = 0;
    event_calls = 0; last_key = last_event = 0;
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
           calls.flags + calls.align + calls.range + calls.value + calls.events + calls.destroy + calls.dialog;
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
static void emit_row(unsigned slot, uint32_t code)
{
#ifndef CANOPUS_MGR_BAND11
    struct widget *w = view.rows[slot];
    w->callback(&code);
#else
    /* Native events have 32-bit pointers at +4 and a code at +8. On a 64-bit
     * host those overlap; only equality is used by the backend, so use an
     * opaque matching token and restore the real binding after dispatch. */
    unsigned char event[24] = {0};
    void *previous = view.row_event_objects[slot], *token;
    memcpy(event + 4, &previous, sizeof(previous));
    memcpy(event + 8, &code, sizeof(uint16_t));
    memcpy(&token, event + 4, sizeof(token));
    view.row_event_objects[slot] = token;
    host_dispatch_row(slot, event);
    view.row_event_objects[slot] = previous;
#endif
}
static void test_identical_and_binding(int baseline)
{
    struct page_options o = options();
    reset(); assert(page(o) == 0); print_counts("initial"); clear_counts();
    assert(page(o) == 0); print_counts("identical");
    if (baseline) return;
    assert(mutations() == 0);
    o.row_event = 777; clear_counts(); assert(page(o) == 0); assert(mutations() == 0);
    emit_row(0, HOST_EVENT_CLICKED);
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
    assert(calls.align > 0); /* Changed wrapping input repositions the dependent suffix. */
    clear_counts(); o.image_path = "/data/other.bin"; assert(page(o) == 0);
    assert(calls.source == 1 && calls.size == 0 && calls.text == 0 && calls.row == 0);
    clear_counts(); o.image_id++; assert(page(o) == 0);
    assert(calls.source == 1 && calls.size == 0 && calls.align == 0);
    clear_counts(); o.image_width++; assert(page(o) == 0);
    assert(calls.size == 1 && calls.source == 0 && calls.text == 0 && calls.row == 0);
    clear_counts(); o.progress++; assert(page(o) == 0);
    assert(calls.value == 1 && mutations() == 1);
}
static void test_reorder_and_reappearance(void)
{
    struct page_options o = options();
    void *first, *second, *label, *image, *progress;
    reset(); assert(page(o) == 0);
    first = view.rows[0]; second = view.rows[1]; label = view.labels[0];
    image = view.images[0]; progress = view.progress[0];
    clear_counts(); o.reverse_rows = 1; assert(page(o) == 0);
    assert(calls.create == 0 && calls.row == 0 && calls.align > 0);
    assert(view.rows[0] == first && view.rows[1] == second);
    clear_counts(); o.omit_media = 1; o.omit_second = 1; assert(page(o) == 0);
    assert(((struct widget *)second)->flags & 1);
    assert(view.bindings[1].event_id == 0);
    assert(((struct widget *)label)->flags & 1);
    assert(((struct widget *)image)->flags & 1);
    assert(((struct widget *)progress)->flags & 1);
    clear_counts(); assert(page(o) == 0); assert(mutations() == 0);
    o.omit_media = 0; o.omit_second = 0; o.label_text = "Restored";
    o.image_path = "/data/restored.bin"; o.progress = 62;
    assert(page(o) == 0); assert(calls.create == 0);
    assert(!(((struct widget *)second)->flags & 1));
    assert(!(((struct widget *)label)->flags & 1));
    assert(!(((struct widget *)image)->flags & 1));
    assert(!(((struct widget *)progress)->flags & 1));
    assert(strcmp(((struct widget *)label)->text, "Restored") == 0);
    assert(strcmp(((struct widget *)image)->source, "/data/restored.bin") == 0);
    assert(((struct widget *)progress)->value == 62);
}
static void test_failure_retry(void)
{
    struct page_options old = options(), proposed;
    reset(); assert(page(old) == 0); proposed = old;
    proposed.row_text = "Partially applied"; proposed.image_width = 0;
    assert(page(proposed) == CANOPUS_UI_ERR_BACKEND);
    assert(strcmp(((struct widget *)view.rows[0])->text, "Partially applied") == 0);
    clear_counts(); assert(page(old) == 0);
    assert(calls.row >= 1 && calls.text >= 1 && calls.source >= 1);
    assert(strcmp(((struct widget *)view.rows[0])->text, old.row_text) == 0);
    clear_counts(); assert(page(old) == 0); assert(mutations() == 0);
}
static int switch_page(void)
{
    struct canopus_ui_tree_v1 *tree = begin();
    assert(CANOPUS_UI_SWITCH_ROW(tree, 2, "Enabled", "", 201, 0, 0) == 0);
    return finish(tree);
}
static void test_disabled_switch(void)
{
    struct widget *w;
    reset(); assert(switch_page() == 0); w = view.rows[0];
    w->checked = 1; clear_counts(); emit_row(0, HOST_EVENT_VALUE_CHANGED);
    assert(event_calls == 0 && w->checked == 0 && calls.row == 1);
    clear_counts(); assert(switch_page() == 0); assert(mutations() == 0);
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
int main(int argc, char **argv)
{
    int baseline = argc > 1 && strcmp(argv[1], "--baseline") == 0;
    test_identical_and_binding(baseline);
    if (baseline) return 0;
    test_property_updates(); test_reorder_and_reappearance(); test_failure_retry();
    test_disabled_switch(); test_dialog_binding(); canopus_ui_host_detach(&view);
    puts("native UI backend regression tests passed");
    return 0;
}
