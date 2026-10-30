/* Native UI ABI2 integration lab; every interaction leaves emulator evidence. */
#include "canopus_module_entry.h"
#include "canopus_ui.h"

enum { HOME, CARD, LIST, DRAG, SELECT };
enum { GO_CARD = 1, GO_LIST, GO_DRAG, GO_SELECT, BACK, UPDATE_CARD,
       CHECK = 30, RADIO_A, RADIO_B, HEADER = 34, POINTER, SCROLL, PICK = 1000 };
enum { LIST_KEY = 10, DRAG_KEY = 20, ROW_HEIGHT = 72, ITEM_COUNT = 100, WINDOW = 10 };
#define ON (CANOPUS_UI_NODE_FLAG_ENABLED | CANOPUS_UI_NODE_FLAG_VISIBLE)
#define LIST_CAPS (CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VISUAL | CANOPUS_UI_CAP_SCROLL | CANOPUS_UI_CAP_VIRTUAL_CONTENT)
#define DRAG_CAPS (CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VISUAL | CANOPUS_UI_CAP_POINTER_EVENTS | CANOPUS_UI_CAP_CAPTURE | CANOPUS_UI_CAP_TRANSLATE)

static struct {
    uint32_t route, caps, width, height, first, selected, checked, radio, header, card, styles;
    int32_t scroll_y, drag_x, drag_y, start_x, start_y, pointer_x, pointer_y;
    uint32_t captured, moved, posting, posted;
} state;

/* Volatile stores keep freestanding aggregate initialization free of libc calls. */
static void clear_visual(struct canopus_ui_visual_v1 *v)
{
    volatile unsigned char *bytes = (volatile unsigned char *)v;
    uint32_t i;
    for (i = 0; i < sizeof(*v); i++) bytes[i] = 0;
}
static uint32_t length(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
static uint32_t number(char *out, uint32_t value)
{
    char rev[12]; uint32_t n = 0, i;
    do { rev[n++] = (char)('0' + value % 10u); value /= 10u; } while (value);
    for (i = 0; i < n; i++) out[i] = rev[n - i - 1u];
    out[n] = 0; return n;
}
static void probe(const struct canopus_context_v1 *ctx, const char *name, uint32_t value)
{
    char line[80]; uint32_t n = 0, i; int32_t fd;
    for (i = 0; name[i]; i++) line[n++] = name[i];
    line[n++] = ' '; n += number(line + n, state.route); line[n++] = '/';
    n += number(line + n, value); line[n++] = '\n';
    fd = ctx->fs->open(ctx, "ui_probe.txt", CANOPUS_FS_WRITE | CANOPUS_FS_APPEND | CANOPUS_FS_CREATE);
    if (fd < 0) return;
    (void)ctx->fs->write(ctx, fd, line, n); (void)ctx->fs->close(ctx, fd);
}
static int32_t component(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                         uint32_t key, uint16_t kind, const char *a, const char *b,
                         uint32_t event, uint32_t flags, int32_t value)
{
    struct canopus_ui_component_props_v1 p = {
        sizeof(p), a, length(a), b, length(b), event, flags, value, 0, 0, 0, 0 };
    return ctx->ui->component(t, key, kind, &p);
}
static int32_t action(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                      uint32_t key, const char *title, const char *detail, uint32_t event)
{
    struct canopus_ui_action_row_props_v1 p = {
        sizeof(p), title, length(title), detail, length(detail), event, 1u };
    return ctx->ui->action_row(t, key, &p);
}
/* Stock nodes have dedicated entry points; canopus_ui_component only takes the
 * extended catalog (LIST..NAVIGATION_HEADER), so section/button go here. */
static int32_t group(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                     uint32_t key, const char *title)
{
    struct canopus_ui_section_props_v1 p = { sizeof(p), title, length(title) };
    return ctx->ui->section(t, key, &p);
}
static int32_t button(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                      uint32_t key, const char *text, uint32_t event)
{
    struct canopus_ui_button_props_v1 p = { sizeof(p), text, length(text), event, 1u };
    return ctx->ui->button(t, key, &p);
}
static int32_t image(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                     uint32_t key, const char *path)
{
    struct canopus_ui_component_props_v1 p = {
        sizeof(p), path, length(path), "", 0, 0,
        CANOPUS_UI_NODE_FLAG_VISIBLE | CANOPUS_UI_NODE_FLAG_ENABLED,
        0, 0, 0, 0, 1u };
    return ctx->ui->component(t, key, CANOPUS_UI_NODE_IMAGE, &p);
}
static int32_t label(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                     uint32_t key, const char *text)
{
    struct canopus_ui_text_props_v1 p = { sizeof(p), text, length(text), CANOPUS_UI_TEXT_BODY };
    return ctx->ui->text(t, key, &p);
}
static int32_t box(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t,
                   uint32_t key, int16_t width, int16_t height, int16_t x, int16_t y,
                   uint32_t color, uint32_t absolute)
{
    struct canopus_ui_layout_v1 l = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 v;
    clear_visual(&v);
    int32_t rc;
    l.width = width; l.height = height;
    v.flags = CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB;
    if (absolute) v.flags |= CANOPUS_UI_VISUAL_FLAG_ABSOLUTE;
    v.background_rgb = color; v.x = x; v.y = y;
    rc = ctx->ui->node_set_layout(t, key, &l);
    return rc == 0 ? ctx->ui->node_set_visual(t, key, &v) : rc;
}
static int32_t build_home(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t)
{
    int32_t rc = action(ctx,t,10,"Card & overlay","Nested layout, RGB and image",GO_CARD);
    if (rc == 0) rc = action(ctx,t,11,"100 items","Continuous virtual scrolling",GO_LIST);
    if (rc == 0) rc = action(ctx,t,12,"Drag","Pointer capture and translation",GO_DRAG);
    if (rc == 0) rc = action(ctx,t,13,"Selections","Checkbox, radio and title",GO_SELECT);
    return rc;
}
static int32_t build_card(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t)
{
    struct canopus_ui_visual_v1 v;
    clear_visual(&v);
    struct canopus_ui_layout_v1 l = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_style_v1 s = CANOPUS_UI_STYLE_DEFAULT;
    uint32_t needed = CANOPUS_UI_CAP_VISUAL | CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_IMAGE;
    int32_t rc;
    if ((state.caps & needed) != needed) return label(ctx,t,10,"Card layout unavailable");
    rc = group(ctx,t,10,state.card == 1u ? "Updated card" : "");
    l.width = (int16_t)(state.width-24u); l.height = 210;
    if (rc == 0) rc = ctx->ui->node_set_layout(t,10,&l);
    if (state.card != 2u) {
        v.flags = CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB;
        v.background_rgb = state.card ? 0x36512bu : 0x15334du;
    }
    /* Extended style set: property IDs beyond colours are audited per target, so
       this block is attempted separately and dropped on ERR_UNSUPPORTED. */
    if (state.styles) {
        s.corner_radius = 16; s.border_width = 3; s.border_color = CANOPUS_UI_COLOR_ACCENT;
        s.opacity = 800;
        v.flags |= CANOPUS_UI_VISUAL_FLAG_OPACITY | CANOPUS_UI_VISUAL_FLAG_CLIP;
    }
    if (rc == 0) rc = ctx->ui->node_set_style(t,10,&s);
    if (rc == 0) rc = ctx->ui->node_set_visual(t,10,&v);
    if (rc == 0) rc = image(ctx,t,11,"/data/canopus/manager_icon.bin");
    if (rc == 0) rc = box(ctx,t,11,96,96,12,state.card == 1u ? 48 : 16,0x15334du,1);
    if (rc == 0 && (state.caps & CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN)) {
        clear_visual(&v);
        v.flags = CANOPUS_UI_VISUAL_FLAG_ABSOLUTE; v.x = 12; v.y = state.card == 1u ? 48 : 16;
        v.image_fit = CANOPUS_UI_IMAGE_FIT_CONTAIN;
        rc = ctx->ui->node_set_visual(t,11,&v);
    }
    if (rc == 0) rc = label(ctx,t,12,"Native overlay");
    l.width = 160; l.height = 50;
    if (rc == 0) rc = ctx->ui->node_set_layout(t,12,&l);
    clear_visual(&v);
    v.flags = CANOPUS_UI_VISUAL_FLAG_ABSOLUTE;
    v.x = 12; v.y = 145;
    if (state.card != 2u) {
        v.flags |= CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB;
        v.foreground_rgb = state.card ? 0xffdd55u : 0xffffffu;
    }
    if (rc == 0) rc = ctx->ui->node_set_visual(t,12,&v);
    if (rc == 0) rc = ctx->ui->end(t);
    if (rc == 0) rc = group(ctx,t,13,"");
    l.width = (int16_t)(state.width-24u); l.height = 90; l.axis = CANOPUS_UI_AXIS_HORIZONTAL;
    l.gap = 8; l.padding_left = 8; l.align = CANOPUS_UI_ALIGN_CENTER;
    if (rc == 0) rc = ctx->ui->node_set_layout(t,13,&l);
    if (rc == 0) rc = label(ctx,t,14,"Left");
    if (rc == 0) rc = label(ctx,t,15,"Right");
    if (rc == 0) rc = ctx->ui->end(t);
    if (rc == 0) rc = action(ctx,t,16,"Update card","Style, heading and reset",UPDATE_CARD);
    return rc;
}
static int32_t build_list(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t)
{
    struct canopus_ui_visual_v1 v;
    clear_visual(&v);
    struct canopus_ui_layout_v1 l = CANOPUS_UI_LAYOUT_DEFAULT;
    uint32_t i; int32_t rc;
    if ((state.caps & LIST_CAPS) != LIST_CAPS) return label(ctx,t,10,"Virtual scroll unavailable");
    rc = component(ctx,t,LIST_KEY,CANOPUS_UI_NODE_SCROLL,"","",SCROLL,ON,0);
    l.width = (int16_t)(state.width-16u); l.height = (int16_t)(state.height-100u);
    v.flags = CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT | CANOPUS_UI_VISUAL_FLAG_CLIP;
    v.content_height = ITEM_COUNT*ROW_HEIGHT; v.scroll_y = state.scroll_y;
    if (rc == 0) rc = ctx->ui->node_set_layout(t,LIST_KEY,&l);
    if (rc == 0) rc = ctx->ui->node_set_visual(t,LIST_KEY,&v);
    for (i = state.first; rc == 0 && i < ITEM_COUNT && i < state.first+WINDOW; i++) {
        char title[24];
        title[0]='I'; title[1]='t'; title[2]='e'; title[3]='m'; title[4]=' ';
        number(title+5,i+1u);
        rc = action(ctx,t,100u+i,title,state.selected == i+1u ? "Selected" : "Tap to select",PICK+i);
        if (rc == 0) rc = box(ctx,t,100u+i,(int16_t)(state.width-24u),ROW_HEIGHT,0,
                             (int16_t)(i*ROW_HEIGHT),state.selected == i+1u ? 0x124b70u : 0x18222cu,1);
    }
    if (rc == 0) rc = ctx->ui->end(t);
    return rc;
}
static int32_t build_drag(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t)
{
    struct canopus_ui_visual_v1 v;
    clear_visual(&v);
    int32_t rc;
    if ((state.caps & DRAG_CAPS) != DRAG_CAPS) return label(ctx,t,10,"Pointer drag unavailable");
    rc = label(ctx,t,10,"Hold and drag the tile");
    if (rc == 0) rc = button(ctx,t,DRAG_KEY,"Drag",POINTER);
    if (rc == 0) rc = box(ctx,t,DRAG_KEY,120,100,20,160,0x245e9du,1);
    v.flags = CANOPUS_UI_VISUAL_FLAG_ABSOLUTE | CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB;
    v.x = 20; v.y = 160; v.background_rgb = 0x245e9du;
    v.translate_x = (int16_t)state.drag_x; v.translate_y = (int16_t)state.drag_y;
    if (rc == 0) rc = ctx->ui->node_set_visual(t,DRAG_KEY,&v);
    return rc;
}
static int32_t build_select(const struct canopus_context_v1 *ctx, struct canopus_ui_tree_v1 *t)
{
    struct canopus_ui_layout_v1 l = CANOPUS_UI_LAYOUT_DEFAULT;
    int32_t rc = component(ctx,t,10,CANOPUS_UI_NODE_CHECKBOX,"Checkbox","",CHECK,
                           ON | (state.checked ? CANOPUS_UI_NODE_FLAG_CHECKED : 0u),0);
    l.width = (int16_t)(state.width-16u); l.height = 104;
    l.padding_left = state.checked ? 20 : 0;
    if (rc == 0) rc = ctx->ui->node_set_layout(t,10,&l);
    if (rc == 0) rc = component(ctx,t,11,CANOPUS_UI_NODE_RADIO_ROW,"Option A","",RADIO_A,
                               ON | (state.radio == 0 ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u),0);
    if (rc == 0) rc = component(ctx,t,12,CANOPUS_UI_NODE_RADIO_ROW,"Option B","",RADIO_B,
                               ON | (state.radio == 1 ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u),0);
    if (rc == 0) rc = action(ctx,t,14,"Change title","Toggle navigation title and back",HEADER);
    return rc;
}
static int32_t render(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui)
{
    struct canopus_ui_tree_v1 *t; int32_t rc = ctx->ui->tree_begin(ui,&t);
    const char *title = state.route == HOME ? "Native UI Lab" :
                        state.route == CARD ? "Card & overlay" : state.route == LIST ? "100 items" :
                        state.route == DRAG ? "Drag" : state.header ? "Saved selections" : "Selections";
    struct canopus_ui_navigation_page_props_v1 p = { sizeof(p),title,length(title) };
    if (rc != 0) return rc;
    rc = ctx->ui->navigation_page(t,1u+state.route*10000u,&p);
    if (rc == 0 && (state.caps & CANOPUS_UI_CAP_NAVIGATION_HEADER)) {
        struct canopus_ui_navigation_header_props_v1 h = {
            sizeof(h),title,length(title),"ABI 2",5,BACK,!state.header,0,0 };
        rc = ctx->ui->navigation_header(t,2,&h);
        /* The header is a container (its trailing title icons); page rows belong
           to the page, so close the header before the body is built. */
        if (rc == 0) rc = ctx->ui->end(t);
    }
    if (rc == 0) {
        switch (state.route) {
        case CARD: rc=build_card(ctx,t); break;
        case LIST: rc=build_list(ctx,t); break;
        case DRAG: rc=build_drag(ctx,t); break;
        case SELECT: rc=build_select(ctx,t); break;
        default: rc=build_home(ctx,t); break;
        }
    }
    if (rc == 0) rc=ctx->ui->end(t);
    if (rc == 0) rc=ctx->ui->tree_commit(t); else ctx->ui->tree_abort(t);
    if (state.styles) {
        if (rc == CANOPUS_UI_ERR_UNSUPPORTED) {
            /* Unaudited style property IDs reject the whole snapshot; retry the
               same page without the extended style set and record the limit. */
            state.styles = 0u;
            probe(ctx,"stylefallback",(uint32_t)(-rc));
            return render(ctx, ui);
        }
        state.styles = 0u;
    }
    if (rc != 0) probe(ctx,"renderfail",(uint32_t)(-rc));
    return rc;
}
static void posted(const struct canopus_context_v1 *ctx, void *user)
{
    (void)user; state.posted++;
    probe(ctx,state.posting ? "post-reentered" : "post",state.posted);
}
static int32_t post_after_callback(const struct canopus_context_v1 *ctx)
{
    int32_t rc;
    state.posting=1;
    rc=ctx->ui->post(ctx,posted,0);
    if (rc == 0) ctx->time->sleep_ms(ctx,1);
    state.posting=0;
    return rc;
}
static int32_t on_open(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui, void *user)
{
    int32_t rc;
    (void)user; state.route=HOME; state.caps=ctx->ui->capabilities(ui);
    ctx->display->get_resolution(ctx,&state.width,&state.height);
    probe(ctx,"open",state.caps);
    rc=render(ctx,ui);
    return rc == 0 ? post_after_callback(ctx) : rc;
}
static int32_t on_event(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui,
                        const struct canopus_ui_event_v1 *e, void *user)
{
    (void)user;
    if (e->event_id == SCROLL && e->type == CANOPUS_UI_EVENT_SCROLL) {
        uint32_t first = (uint32_t)e->scroll_y/ROW_HEIGHT;
        if (first > ITEM_COUNT-WINDOW) first=ITEM_COUNT-WINDOW;
        state.scroll_y=e->scroll_y;
        if (first == state.first) return 0;
        state.first=first; probe(ctx,"window",first); return render(ctx,ui);
    }
    if (e->event_id == POINTER) {
        if (e->type == CANOPUS_UI_EVENT_PRESS) {
            int32_t rc=ctx->ui->control(ui,e->generation,e->key,CANOPUS_UI_CONTROL_CAPTURE,0,0,0);
            if (rc != 0) return rc;
            state.captured=1; state.moved=0; state.start_x=state.drag_x; state.start_y=state.drag_y;
            state.pointer_x=e->x; state.pointer_y=e->y; probe(ctx,"press",1);
        } else if (e->type == CANOPUS_UI_EVENT_MOVE && state.captured) {
            state.drag_x=state.start_x+e->x-state.pointer_x;
            state.drag_y=state.start_y+e->y-state.pointer_y;
            if (!state.moved && (state.drag_x != state.start_x || state.drag_y != state.start_y)) {
                state.moved=1; probe(ctx,"move",1);
            }
            return ctx->ui->control(ui,e->generation,e->key,CANOPUS_UI_CONTROL_TRANSLATE,state.drag_x,state.drag_y,0);
        } else if ((e->type == CANOPUS_UI_EVENT_RELEASE || e->type == CANOPUS_UI_EVENT_CANCEL) && state.captured) {
            state.captured=0; probe(ctx,"release",1);
            return ctx->ui->control(ui,e->generation,e->key,CANOPUS_UI_CONTROL_RELEASE_CAPTURE,0,0,0);
        }
        return 0;
    }
    if (e->type != CANOPUS_UI_EVENT_ACTIVATE && e->type != CANOPUS_UI_EVENT_VALUE_CHANGED) return 0;
    if (state.route == LIST && e->key >= 100u && e->key < 100u+ITEM_COUNT) {
        state.selected=e->key-99u; probe(ctx,"pick",state.selected); return render(ctx,ui);
    }
    switch (e->event_id) {
    case GO_CARD: state.route=CARD; state.card=0; break;
    case UPDATE_CARD:
        state.card=(state.card+1u)%3u; probe(ctx,"card",state.card);
        if (post_after_callback(ctx) != 0) return CANOPUS_UI_ERR_BACKEND;
        state.styles = (state.card == 1u);
        break;
    case GO_LIST: state.route=LIST; break;
    case GO_DRAG: state.route=DRAG; break;
    case GO_SELECT: state.route=SELECT; break;
    case BACK:
        if (state.route == HOME) return ctx->ui->close_page(ctx);
        state.route=HOME; state.header=0; break;
    case CHECK: state.checked=!state.checked; probe(ctx,"check",state.checked); break;
    case RADIO_A: state.radio=0; probe(ctx,"radio",0); break;
    case RADIO_B: state.radio=1; probe(ctx,"radio",1); break;
    case HEADER: state.header=!state.header; probe(ctx,"header",state.header); break;
    default: return CANOPUS_UI_ERR_ARGUMENT;
    }
    probe(ctx,"route",state.route); return render(ctx,ui);
}
static int32_t on_back(const struct canopus_context_v1 *ctx, struct canopus_ui_context_v1 *ui, void *user)
{
    (void)user; probe(ctx,"back",state.route);
    if (state.route == HOME) return 1;
    state.route=HOME; state.header=0; return render(ctx,ui);
}
static void on_close(const struct canopus_context_v1 *ctx, void *user)
{ (void)user; state.captured=0; probe(ctx,"close",0); }
static const struct canopus_ui_entry_v1 entry = {sizeof(entry),"Native UI Lab",on_open,on_event,on_back,on_close,0,0};
static int32_t prepare(const struct canopus_context_v1 *ctx) { (void)ctx; return 0; }
static int32_t activate(const struct canopus_context_v1 *ctx) { return ctx->ui->set_entry(ctx,&entry); }
static int32_t stop(const struct canopus_context_v1 *ctx) { return ctx->ui->set_entry(ctx,0); }
static int32_t query(struct canopus_status_writer_v1 *writer) { (void)writer; return 0; }
static const struct canopus_module_descriptor_v1 descriptor = {
    sizeof(descriptor),CANOPUS_ABI_MAJOR,CANOPUS_ABI_MINOR,0,"native_ui_lab","1.0.0","native-ui-lab",
    CANOPUS_TARGET_ID,prepare,activate,stop,stop,query,0,0,
};
CANOPUS_MODULE_ENTRY(descriptor)
