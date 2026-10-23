/* Production input state machine with counted native primitives. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "canopus_ui_host.h"
#define canopus_memset memset
#define HOST_CONTENT_HEIGHT 100
struct object { int32_t scroll_y; uint32_t flags; int32_t x, y; };
static struct object objects[4];
static struct canopus_ui_host_view view;
static struct canopus_ui_context_v1 context;
static struct canopus_ui_event_v1 events[32];
static uint32_t event_count, now_ms = 1000, scroll_calls, translate_calls;
static int capture_on_press, detach_on_release;
static int32_t host_control(void *, uint32_t, canopus_ui_node_id, uint32_t, int32_t, int32_t, void *);
static uint32_t now(struct canopus_ui_host_view *v) { (void)v; return now_ms; }
static const struct canopus_ui_host_hooks hooks = { .time_ms = now };
static int32_t receive_event(void *cookie, const struct canopus_ui_event_v1 *event)
{
    assert(cookie == &context);
    assert(event->generation == context.committed.generation);
    assert(event_count < 32);
    events[event_count++] = *event;
    if (capture_on_press && event->type == CANOPUS_UI_EVENT_PRESS)
        assert(canopus_ui_control(&context, event->generation, event->key,
                                  CANOPUS_UI_CONTROL_CAPTURE, 0, 0, NULL) == 0);
    if (detach_on_release && event->type == CANOPUS_UI_EVENT_RELEASE)
        memset(&view, 0, sizeof(view));
    return 0;
}
static void host_object_flag(void *object, uint32_t flag, int add)
{
    struct object *o = object;
    if (add) o->flags |= flag; else o->flags &= ~flag;
}
static void *host_input_object(struct canopus_ui_host_view *v, uint32_t key)
{
    unsigned i;
    for (i = 0; i < CANOPUS_UI_MAX_NODES; i++)
        if (v->nodes[i].visible && v->nodes[i].key == key) return v->nodes[i].object;
    return NULL;
}
static int32_t host_input_translate(struct canopus_ui_host_view *v, uint32_t key,
                                    int32_t x, int32_t y)
{
    struct object *o = host_input_object(v, key);
    assert(o != NULL); translate_calls++; o->x = x; o->y = y; return 0;
}
static int32_t get_scroll(void *o) { return ((struct object *)o)->scroll_y; }
static void scroll_to(void *o, int32_t y, uint32_t animation)
{ assert(animation == 0); ((struct object *)o)->scroll_y = y; scroll_calls++; }
struct native_event { uint32_t code; uint32_t reserved; uint16_t band11_code; };
static int32_t input_point[2];
static void *event_indev(void *e) { return e; }
static void get_point(void *e, int32_t *p)
{ (void)e; memcpy(p, input_point, 2 * sizeof(*p)); }
static void get_coords(void *o, int32_t *area)
{ (void)o; area[0] = 20; area[1] = 68; area[2] = 120; area[3] = 168; }
#ifndef CANOPUS_MGR_BAND11
static uint32_t get_code(void *e) { return ((struct native_event *)e)->code; }
#define FW_LVX_EVENT_GET_CODE get_code
#endif
#define FW_UI_COORD_TYPE int32_t
#define FW_UI_EVENT_GET_INDEV event_indev
#define FW_UI_INDEV_GET_POINT get_point
#define FW_UI_OBJECT_GET_COORDS get_coords
#define FW_UI_OBJECT_GET_SCROLL_Y get_scroll
#define FW_UI_OBJECT_SCROLL_TO_Y scroll_to
/* Test codes are deliberately different from LVGL values: the adapter must use
 * target-provided enum bindings rather than an assumed library version. */
#define FW_UI_EVENT_PRESSED 101u
#define FW_UI_EVENT_PRESSING 102u
#define FW_UI_EVENT_LONG_PRESSED 103u
#define FW_UI_EVENT_RELEASED 104u
#define FW_UI_EVENT_PRESS_LOST 105u
#define FW_UI_EVENT_SCROLL 106u
#include "../../manager/target/canopus_ui_input.inc"

/* Native allocation is covered by test_ui_backend.c. This fixture keeps the
 * three native objects and exercises input publication through real commits. */
static int32_t apply_snapshot(void *cookie, const struct canopus_ui_snapshot_v1 *snapshot)
{
    unsigned i;
    assert(cookie == &view);
    host_input_prepare(&view, snapshot);
    host_input_before_replace(&view);
    for (i = 0; i < snapshot->node_count; i++) {
        const struct canopus_ui_node_v1 *node = &snapshot->nodes[i];
        view.nodes[i].key = node->key;
        view.nodes[i].type = node->type;
        view.nodes[i].parent_key = node->parent == CANOPUS_UI_NO_NODE
                                      ? 0 : snapshot->nodes[node->parent].key;
        view.nodes[i].visible = (node->flags & CANOPUS_UI_NODE_FLAG_VISIBLE) != 0 &&
            (node->parent == CANOPUS_UI_NO_NODE || view.nodes[node->parent].visible);
        view.nodes[i].binding = (struct canopus_ui_host_binding){
            snapshot->generation, node->key, node->event_id
        };
    }
    view.rendered_generation = snapshot->generation;
    host_input_publish(&view, snapshot, 700, 0);
    return CANOPUS_UI_OK;
}
static const struct canopus_ui_backend_v1 backend = {
    sizeof(backend), CANOPUS_UI_ABI_MAJOR, CANOPUS_UI_ABI_MINOR,
    HOST_INPUT_CAPS, apply_snapshot, host_control
};
static void commit_snapshot(const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct canopus_ui_tree_v1 *tree;
    uint32_t generation;
    assert(canopus_ui_tree_begin(&context, &tree) == 0);
    generation = tree->_snapshot.generation;
    tree->_snapshot = *snapshot;
    tree->_snapshot.generation = generation;
    assert(canopus_ui_tree_commit(tree) == 0);
}
static void reset(void)
{
    unsigned i;
    host_input_forget(&view);
    memset(&view, 0, sizeof(view));
    assert(canopus_ui_context_init(&context, &backend, &view, receive_event, &context) == 0);
    memset(objects, 0, sizeof(objects));
    event_count = scroll_calls = translate_calls = 0;
    capture_on_press = detach_on_release = 0;
    view.ui = &context; view.active = view.interactive = view.render_valid = 1;
    view.rendered_generation = context.committed.generation = 9;
    view.hooks = &hooks; view.content_root = &objects[0];
    context.has_committed = 1; context.committed.node_count = 3;
    for (i = 0; i < 3; i++) {
        view.nodes[i].object = &objects[i]; view.nodes[i].key = i + 1;
        view.nodes[i].visible = 1; view.nodes[i].height = 100;
        view.nodes[i].binding = (struct canopus_ui_host_binding){9, i + 1, 40 + i};
        context.committed.nodes[i].key = i + 1;
        context.committed.nodes[i].event_id = 40 + i;
        context.committed.nodes[i].parent = i == 0 ? CANOPUS_UI_NO_NODE : (uint16_t)(i - 1);
        context.committed.nodes[i].flags = CANOPUS_UI_NODE_FLAG_VISIBLE;
        objects[i].flags = 16;
    }
    view.nodes[0].type = CANOPUS_UI_NODE_NAVIGATION_PAGE;
    context.committed.nodes[0].type = CANOPUS_UI_NODE_NAVIGATION_PAGE;
    view.nodes[1].type = context.committed.nodes[1].type = CANOPUS_UI_NODE_SCROLL;
    view.nodes[1].parent_key = 1;
    view.nodes[2].type = context.committed.nodes[2].type = CANOPUS_UI_NODE_ACTION_ROW;
    context.committed.nodes[2].flags |= CANOPUS_UI_NODE_FLAG_ENABLED;
    view.nodes[2].parent_key = 2;
    view.nodes[2].y = 400; view.nodes[2].height = 100;
    context.committed.visuals[1].content_height = 900;
    host_input_publish(&view, &context.committed, 700, 0);
}
static int route(unsigned node, uint32_t type, int x, int y)
{ return host_input_route(&view, &view.nodes[node].binding, type, now_ms++, x, y); }
static void pointer_sequence(void)
{
    reset(); capture_on_press = 1;
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
    assert(!(objects[0].flags & 16) && !(objects[1].flags & 16));
    assert(route(0, CANOPUS_UI_EVENT_MOVE, 11, 19) == 0);
    assert(events[1].key == 3 && events[1].delta_x == 6 && events[1].delta_y == 11);
    assert(events[1].scroll_y == 0 && events[1].viewport_height == 100 && events[1].content_height == 900);
    assert(route(2, CANOPUS_UI_EVENT_LONG_PRESS, 11, 19) == 0);
    assert(route(0, CANOPUS_UI_EVENT_RELEASE, 12, 20) == 0);
    assert((objects[0].flags & 16) && (objects[1].flags & 16));
    assert(event_count == 4 && events[3].type == CANOPUS_UI_EVENT_RELEASE);
    assert(route(2, CANOPUS_UI_EVENT_MOVE, 30, 30) == CANOPUS_UI_ERR_STALE);
}
static void capture_last_slot_restores_ancestors(void)
{
    const unsigned last = CANOPUS_UI_MAX_NODES - 1u;
    reset(); capture_on_press = 1;
    /* Key order and slot order differ; the highest native slot is an ancestor. */
    view.nodes[last] = view.nodes[1];
    memset(&view.nodes[1], 0, sizeof(view.nodes[1]));
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
    assert(!(objects[0].flags & 16) && !(objects[1].flags & 16));
    assert(route(last, CANOPUS_UI_EVENT_RELEASE, 6, 9) == 0);
    assert((objects[0].flags & 16) && (objects[1].flags & 16));
    assert(event_count == 2 && events[1].key == 3);
    assert(events[1].type == CANOPUS_UI_EVENT_RELEASE);
    host_input_cancel(&view);
    assert(event_count == 2);
}
static void cancel_and_generation(void)
{
    reset(); capture_on_press = 1;
    view.nodes[1].scroll_locked = 1; objects[1].flags = 0;
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 1, 2) == 0);
    host_input_cancel(&view);
    assert(event_count == 2 && events[1].type == CANOPUS_UI_EVENT_CANCEL);
    assert(events[1].time_ms == now_ms && events[1].x == 1 && events[1].y == 2);
    assert((objects[0].flags & 16) && !(objects[1].flags & 16));
    host_input_cancel(&view); assert(event_count == 2);
    context.committed.generation = view.rendered_generation = 10;
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 3, 4) == CANOPUS_UI_ERR_STALE);
    assert(host_control(&view, 9, 3, CANOPUS_UI_CONTROL_TRANSLATE, 1, 2, NULL) == CANOPUS_UI_ERR_STALE);
}
static void capture_window_commit(void)
{
    struct canopus_ui_snapshot_v1 next;
    struct canopus_ui_host_binding previous;
    reset(); capture_on_press = 1;
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
    previous = view.nodes[2].binding;
    next = context.committed;
    commit_snapshot(&next);
    assert(event_count == 1);
    assert(!(objects[0].flags & 16) && !(objects[1].flags & 16));
    assert(host_input_route(&view, &previous, CANOPUS_UI_EVENT_MOVE, now_ms, 6, 10) == CANOPUS_UI_ERR_STALE);
    assert(route(2, CANOPUS_UI_EVENT_MOVE, 6, 10) == 0);
    assert(events[1].generation == 10 && events[1].key == 3);
    next.nodes[0].key = 99;
    commit_snapshot(&next);
    assert(event_count == 3 && events[2].type == CANOPUS_UI_EVENT_CANCEL);
    assert(events[2].generation == 10);
    assert(objects[0].flags & 16);
}
static void invalidated_target_cancels(void)
{
    unsigned change;
    for (change = 0; change < 4; change++) {
        struct canopus_ui_snapshot_v1 next;
        reset(); capture_on_press = 1;
        assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
        next = context.committed;
        /* Disabled target, then hidden target, scroll ancestor and page. */
        if (change == 0) next.nodes[2].flags &= ~CANOPUS_UI_NODE_FLAG_ENABLED;
        else next.nodes[3 - change].flags &= ~CANOPUS_UI_NODE_FLAG_VISIBLE;
        commit_snapshot(&next);
        assert(event_count == 2 && events[1].type == CANOPUS_UI_EVENT_CANCEL);
        assert(events[1].generation == 9 && events[1].key == 3 && events[1].event_id == 42);
        assert(events[1].x == 5 && events[1].y == 8 && events[1].time_ms == now_ms);
        assert(!host_input_state(&view)->pressed && !host_input_state(&view)->captured);
        assert((objects[0].flags & 16) && (objects[1].flags & 16));
        assert(route(2, CANOPUS_UI_EVENT_RELEASE, 6, 10) == CANOPUS_UI_ERR_STALE);
        commit_snapshot(&next);
        assert(event_count == 2); /* Repeated invalidation cannot duplicate CANCEL. */
    }
}
static void capture_transfer_invalidated(void)
{
    unsigned change;
    for (change = 0; change < 2; change++) {
        struct canopus_ui_snapshot_v1 next;
        reset();
        assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
        assert(canopus_ui_control(&context, 9, 1, CANOPUS_UI_CONTROL_CAPTURE, 0, 0, NULL) == 0);
        next = context.committed;
        if (change == 0) next.nodes[2].flags &= ~CANOPUS_UI_NODE_FLAG_ENABLED;
        else next.nodes[0].event_id++;
        commit_snapshot(&next);
        assert(event_count == 2 && events[1].type == CANOPUS_UI_EVENT_CANCEL);
        assert(events[1].generation == 9 && events[1].key == 1 && events[1].event_id == 40);
        assert(!host_input_state(&view)->pressed && !host_input_state(&view)->captured);
        assert(objects[0].flags & 16);
        assert(route(2, CANOPUS_UI_EVENT_RELEASE, 6, 10) == CANOPUS_UI_ERR_STALE);
        assert(event_count == 2);
    }
}
static void capture_transfer_release(int commit)
{
    struct canopus_ui_snapshot_v1 next;
    reset();
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 5, 8) == 0);
    assert(canopus_ui_control(&context, 9, 2, CANOPUS_UI_CONTROL_CAPTURE, 0, 0, NULL) == 0);
    assert(!(objects[0].flags & 16) && !(objects[1].flags & 16));
    if (commit) {
        next = context.committed;
        commit_snapshot(&next);
        assert(event_count == 1); /* Scroll containers do not require ENABLED. */
    }
    assert(route(0, CANOPUS_UI_EVENT_MOVE, 9, 12) == 0);
    assert(events[1].key == 2 && events[1].event_id == 41);
    assert(events[1].generation == context.committed.generation);
    assert(events[1].delta_x == 4 && events[1].delta_y == 4);
    assert(canopus_ui_control(&context, context.committed.generation, 3,
                              CANOPUS_UI_CONTROL_RELEASE_CAPTURE, 0, 0, NULL) == CANOPUS_UI_ERR_STATE);
    assert(canopus_ui_control(&context, context.committed.generation, 2,
                              CANOPUS_UI_CONTROL_RELEASE_CAPTURE, 0, 0, NULL) == 0);
    assert(!host_input_state(&view)->captured && host_input_state(&view)->pressed);
    assert((objects[0].flags & 16) && (objects[1].flags & 16));
    assert(route(1, CANOPUS_UI_EVENT_MOVE, 11, 15) == 0);
    assert(events[2].key == 3 && events[2].event_id == 42);
    assert(events[2].generation == context.committed.generation);
    assert(events[2].delta_x == 2 && events[2].delta_y == 3);
    assert(route(1, CANOPUS_UI_EVENT_RELEASE, 12, 17) == 0);
    assert(event_count == 4 && events[3].key == 3 && events[3].event_id == 42);
    assert(events[3].type == CANOPUS_UI_EVENT_RELEASE && events[3].delta_y == 2);
}
static void controls(void)
{
    struct canopus_ui_scroll_metrics_v1 metrics;
    reset();
    assert(host_control(&view, 9, 2, CANOPUS_UI_CONTROL_SCROLL_QUERY, 0, 0, &metrics) == 0);
    assert(metrics.y == 0 && metrics.viewport_height == 100 && metrics.content_height == 900);
    assert(host_control(&view, 9, 2, CANOPUS_UI_CONTROL_SCROLL_TO, 9999, 0, NULL) == 0);
    assert(objects[1].scroll_y == 800 && scroll_calls == 1);
    assert(host_control(&view, 9, 2, CANOPUS_UI_CONTROL_SCROLL_TO, -50, 0, NULL) == 0);
    assert(objects[1].scroll_y == 0);
    assert(host_control(&view, 9, 3, CANOPUS_UI_CONTROL_TRANSLATE, 10, -35, NULL) == 0);
    assert(translate_calls == 1 && objects[2].x == 10 && objects[2].y == -35);
    assert(host_control(&view, 9, 3, CANOPUS_UI_CONTROL_CAPTURE, 0, 0, NULL) == CANOPUS_UI_ERR_STATE);
    assert(route(2, CANOPUS_UI_EVENT_PRESS, 1, 2) == 0);
    assert(host_control(&view, 9, 3, CANOPUS_UI_CONTROL_CAPTURE, 0, 0, NULL) == 0);
    assert(host_control(&view, 9, 2, CANOPUS_UI_CONTROL_RELEASE_CAPTURE, 0, 0, NULL) == CANOPUS_UI_ERR_STATE);
    assert(host_control(&view, 9, 3, CANOPUS_UI_CONTROL_RELEASE_CAPTURE, 0, 0, NULL) == 0);
    assert(objects[0].flags & 16);
    host_input_cancel(&view);
}
static void native_coordinates(void)
{
    struct native_event e = { FW_UI_EVENT_PRESSED, 0, FW_UI_EVENT_PRESSED };
    reset(); capture_on_press = 1; input_point[0] = 75; input_point[1] = 110;
    host_input_native(&view, &view.nodes[2].binding, &e);
    assert(event_count == 1 && events[0].x == 55 && events[0].y == 42);
    assert(events[0].time_ms == now_ms);
    assert(!(objects[0].flags & 16) && !(objects[1].flags & 16));
    detach_on_release = 1;
    assert(route(2, CANOPUS_UI_EVENT_RELEASE, 56, 43) == 0);
    assert(view.ui == NULL); /* dispatch may destroy the page synchronously */
    assert((objects[0].flags & 16) && (objects[1].flags & 16));
    host_input_forget(&view);
}
int main(void)
{
    pointer_sequence(); capture_last_slot_restores_ancestors(); cancel_and_generation();
    capture_window_commit(); controls(); native_coordinates();
    invalidated_target_cancels(); capture_transfer_invalidated();
    capture_transfer_release(0); capture_transfer_release(1);
    puts("native UI input tests passed"); return 0;
}
