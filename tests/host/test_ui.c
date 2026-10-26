/* Host tests: bounded semantic native UI tree. */
#include "canopus_test.h"
#include "canopus_ui.h"
#include <stddef.h>
#include <string.h>

struct fake_ui_backend {
    struct canopus_ui_snapshot_v1 applied;
    uint32_t apply_count;
    int fail;
    uint32_t control_count;
    uint32_t control_generation;
    canopus_ui_node_id control_key;
    uint32_t control_op;
    int32_t control_a;
    int32_t control_b;
    int32_t control_result;
};

struct fake_event_sink {
    uint32_t call_count;
    struct canopus_ui_event_v1 event;
    int32_t result;
};

static int32_t fake_apply(void *cookie,
                          const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct fake_ui_backend *fake = (struct fake_ui_backend *)cookie;
    fake->apply_count++;
    if (fake->fail) {
        return fake->fail;
    }
    fake->applied = *snapshot;
    return 0;
}

static int32_t fake_event(void *cookie,
                          const struct canopus_ui_event_v1 *event)
{
    struct fake_event_sink *sink = (struct fake_event_sink *)cookie;
    sink->call_count++;
    sink->event = *event;
    return sink->result;
}

static int32_t fake_control(void *cookie, uint32_t generation,
                            canopus_ui_node_id key, uint32_t op,
                            int32_t a, int32_t b, void *out)
{
    struct fake_ui_backend *fake = (struct fake_ui_backend *)cookie;
    fake->control_count++;
    fake->control_generation = generation;
    fake->control_key = key;
    fake->control_op = op;
    fake->control_a = a;
    fake->control_b = b;
    if (fake->control_result == CANOPUS_UI_OK &&
        op == CANOPUS_UI_CONTROL_SCROLL_QUERY) {
        struct canopus_ui_scroll_metrics_v1 *metrics = out;
        metrics->y = 40;
        metrics->viewport_height = 100;
        metrics->content_height = 500;
    }
    return fake->control_result;
}

#define FAKE_CAPABILITIES (CANOPUS_UI_CAP_EXTENDED_COMPONENTS | \
    CANOPUS_UI_CAP_STYLE | CANOPUS_UI_CAP_LAYOUT | CANOPUS_UI_CAP_VALUES | \
    CANOPUS_UI_CAP_NAVIGATION_HEADER | CANOPUS_UI_CAP_IMAGE | \
    CANOPUS_UI_CAP_PROGRESS | CANOPUS_UI_CAP_VISUAL | CANOPUS_UI_CAP_SCROLL | \
    CANOPUS_UI_CAP_TRANSLATE | CANOPUS_UI_CAP_CAPTURE | \
    CANOPUS_UI_CAP_POINTER_EVENTS | CANOPUS_UI_CAP_IMAGE_FIT_CONTAIN | \
    CANOPUS_UI_CAP_IMAGE_FIT_COVER | CANOPUS_UI_CAP_IMAGE_FIT_STRETCH | \
    CANOPUS_UI_CAP_VIRTUAL_CONTENT | CANOPUS_UI_CAP_FONT)

static const struct canopus_ui_backend_v1 fake_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    FAKE_CAPABILITIES,
    fake_apply,
    fake_control,
};

static int32_t activate(struct canopus_ui_context_v1 *context,
                        uint32_t generation, canopus_ui_node_id key,
                        uint32_t event_id)
{
    const struct canopus_ui_event_v1 event = {
        .struct_size = sizeof(event), .generation = generation,
        .key = key, .event_id = event_id, .type = CANOPUS_UI_EVENT_ACTIVATE
    };
    return canopus_ui_dispatch_event(context, &event);
}

static void init_context(struct canopus_ui_context_v1 *context,
                         struct fake_ui_backend *backend,
                         struct fake_event_sink *sink)
{
    memset(backend, 0, sizeof(*backend));
    if (sink != 0) {
        memset(sink, 0, sizeof(*sink));
    }
    CHECK(canopus_ui_context_init(context, &fake_backend, backend,
                                  sink != 0 ? fake_event : 0, sink) ==
          CANOPUS_UI_OK);
}

static struct canopus_ui_tree_v1 *begin_page(
    struct canopus_ui_context_v1 *context, const char *title)
{
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 props = {
        sizeof(props), title, (uint32_t)strlen(title)
    };
    CHECK(canopus_ui_tree_begin(context, &tree) == CANOPUS_UI_OK);
    CHECK(tree != 0);
    if (tree != 0) {
        CHECK(canopus_ui_navigation_page(tree, 1, &props) == CANOPUS_UI_OK);
    }
    return tree;
}

TEST(ui_builds_linked_tree_and_copies_strings)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_section_props_v1 section = {
        sizeof(section), "Framework", 9
    };
    struct canopus_ui_status_row_props_v1 status = {
        sizeof(status), "Supervisor", 10, "Ready", 5
    };
    struct canopus_ui_button_props_v1 button = {
        sizeof(button), "Install", 7, 42, 1
    };
    const struct canopus_ui_snapshot_v1 *snapshot;

    init_context(&context, &backend, 0);
    tree = begin_page(&context, "Canopus Manager");
    CHECK(canopus_ui_section(tree, 2, &section) == CANOPUS_UI_OK);
    CHECK(canopus_ui_status_row(tree, 3, &status) == CANOPUS_UI_OK);
    CHECK(canopus_ui_button(tree, 4, &button) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);

    snapshot = canopus_ui_current(&context);
    CHECK(snapshot != 0);
    CHECK(snapshot->node_count == 4);
    CHECK(snapshot->generation == 1);
    CHECK(snapshot->nodes[0].parent == CANOPUS_UI_NO_NODE);
    CHECK(snapshot->nodes[0].first_child == 1);
    CHECK(snapshot->nodes[1].parent == 0);
    CHECK(snapshot->nodes[1].first_child == 2);
    CHECK(snapshot->nodes[2].next_sibling == 3);
    CHECK(snapshot->nodes[3].next_sibling == CANOPUS_UI_NO_NODE);
    CHECK(strcmp(snapshot->strings + snapshot->nodes[0].primary_off,
                 "Canopus Manager") == 0);
    CHECK(strcmp(snapshot->strings + snapshot->nodes[2].secondary_off,
                 "Ready") == 0);
    CHECK(backend.apply_count == 1);
    CHECK(backend.applied.node_count == 4);
}

TEST(ui_rejects_invalid_shape_and_duplicate_keys)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_text_props_v1 text = { sizeof(text), "x", 1, 0 };
    struct canopus_ui_navigation_page_props_v1 page = {
        sizeof(page), "nested", 6
    };

    init_context(&context, &backend, 0);
    CHECK(canopus_ui_tree_begin(&context, &tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_text(tree, 2, &text) == CANOPUS_UI_ERR_STATE);
    CHECK(canopus_ui_navigation_page(tree, 1, &page) == CANOPUS_UI_OK);
    CHECK(canopus_ui_navigation_page(tree, 2, &page) == CANOPUS_UI_ERR_STATE);
    CHECK(canopus_ui_text(tree, 1, &text) == CANOPUS_UI_ERR_DUP_KEY);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_ERR_STATE);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
}

TEST(ui_context_drops_a_tree_its_owner_left_open)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree = 0;

    init_context(&context, &backend, 0);
    tree = begin_page(&context, "left open");
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_ERR_STATE);
    CHECK(canopus_ui_tree_begin(&context, &tree) == CANOPUS_UI_ERR_STATE);
    canopus_ui_context_abort_pending(&context);
    CHECK(canopus_ui_tree_begin(&context, &tree) == CANOPUS_UI_OK);
}

TEST(ui_enforces_node_and_depth_capacity)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_text_props_v1 text = { sizeof(text), 0, 0, 0 };
    struct canopus_ui_section_props_v1 section = { sizeof(section), 0, 0 };
    uint32_t i;

    init_context(&context, &backend, 0);
    tree = begin_page(&context, "");
    for (i = 0; i < CANOPUS_UI_MAX_NODES - 1u; i++) {
        CHECK(canopus_ui_text(tree, 100u + i, &text) == CANOPUS_UI_OK);
    }
    CHECK(canopus_ui_text(tree, 999, &text) == CANOPUS_UI_ERR_CAPACITY);
    canopus_ui_tree_abort(tree);

    tree = begin_page(&context, "");
    for (i = 1; i < CANOPUS_UI_MAX_DEPTH; i++) {
        CHECK(canopus_ui_section(tree, 200u + i, &section) == CANOPUS_UI_OK);
    }
    CHECK(canopus_ui_section(tree, 999, &section) == CANOPUS_UI_ERR_CAPACITY);
    for (i = 0; i < CANOPUS_UI_MAX_DEPTH; i++) {
        CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    }
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_ERR_STATE);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
}

TEST(ui_string_failure_rolls_back_entire_node)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_status_row_props_v1 status;
    char oversized[CANOPUS_UI_STRING_CAPACITY];
    uint16_t before;

    memset(oversized, 'x', sizeof(oversized));
    init_context(&context, &backend, 0);
    tree = begin_page(&context, "R");
    before = tree->_snapshot.string_used;
    status.struct_size = sizeof(status);
    status.label = "A";
    status.label_len = 1;
    status.value = oversized;
    status.value_len = sizeof(oversized) - 2u;
    CHECK(canopus_ui_status_row(tree, 2, &status) == CANOPUS_UI_ERR_CAPACITY);
    CHECK(tree->_snapshot.node_count == 1);
    CHECK(tree->_snapshot.string_used == before);
    CHECK(strcmp(tree->_snapshot.strings, "R") == 0);
    canopus_ui_tree_abort(tree);
}

TEST(ui_backend_failure_preserves_previous_commit)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree;
    const struct canopus_ui_snapshot_v1 *snapshot;

    init_context(&context, &backend, 0);
    tree = begin_page(&context, "old");
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);

    backend.fail = 1;
    tree = begin_page(&context, "new");
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_ERR_BACKEND);
    snapshot = canopus_ui_current(&context);
    CHECK(snapshot != 0);
    CHECK(snapshot->generation == 1);
    CHECK(strcmp(snapshot->strings + snapshot->nodes[0].primary_off, "old") == 0);
    CHECK(context.building == 0);

    backend.fail = 0;
    tree = begin_page(&context, "next");
    CHECK(tree->_snapshot.generation == 2);
    canopus_ui_tree_abort(tree);
}

TEST(ui_events_are_generation_checked_and_interactive_only)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct fake_event_sink sink;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_button_props_v1 enabled = {
        sizeof(enabled), "Enable", 6, 77, 1
    };
    struct canopus_ui_button_props_v1 disabled = {
        sizeof(disabled), "Remove", 6, 88, 0
    };
    struct canopus_ui_text_props_v1 text = { sizeof(text), "body", 4, 0 };
    struct canopus_ui_switch_row_props_v1 toggle = {
        sizeof(toggle), "Safe mode", 9, "Recovery", 8, 99, 1, 1
    };

    init_context(&context, &backend, &sink);
    tree = begin_page(&context, "Manager");
    CHECK(canopus_ui_button(tree, 2, &enabled) == CANOPUS_UI_OK);
    CHECK(canopus_ui_button(tree, 3, &disabled) == CANOPUS_UI_OK);
    CHECK(canopus_ui_text(tree, 4, &text) == CANOPUS_UI_OK);
    CHECK(canopus_ui_switch_row(tree, 5, &toggle) == CANOPUS_UI_OK);
    CHECK((tree->_snapshot.nodes[4].flags & CANOPUS_UI_NODE_FLAG_CHECKED) != 0u);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);

    sink.result = 19;
    CHECK(activate(&context, 0, 2, 77) ==
          CANOPUS_UI_ERR_STALE);
    CHECK(context.dropped_events == 1);
    CHECK(sink.call_count == 0);
    CHECK(activate(&context, 1, 3, 88) ==
          CANOPUS_UI_ERR_DISABLED);
    CHECK(activate(&context, 1, 4, 77) ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(activate(&context, 1, 2, 78) ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(activate(&context, 1, 5, 99) == 19);
    CHECK(activate(&context, 1, 2, 77) == 19);
    CHECK(sink.call_count == 2);
    CHECK(sink.event.generation == 1);
    CHECK(sink.event.key == 2);
    CHECK(sink.event.event_id == 77);
}

TEST(ui_validates_backend_and_property_headers)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend state;
    struct canopus_ui_backend_v1 backend = fake_backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_button_props_v1 button = {
        sizeof(button), "x", 1, 0, 1
    };

    CHECK(canopus_ui_context_init(0, &fake_backend, &state, 0, 0) ==
          CANOPUS_UI_ERR_ARGUMENT);
    backend.abi_major++;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) ==
          CANOPUS_UI_ERR_ARGUMENT);
    init_context(&context, &state, 0);
    tree = begin_page(&context, "x");
    CHECK(canopus_ui_button(tree, 2, &button) == CANOPUS_UI_ERR_ARGUMENT);
    button.event_id = 1;
    button.struct_size = 0;
    CHECK(canopus_ui_button(tree, 2, &button) == CANOPUS_UI_ERR_ARGUMENT);
    canopus_ui_tree_abort(tree);
}

TEST(ui_extended_catalog_supports_style_layout_and_values)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct fake_event_sink sink;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_component_props_v1 bar = {
        sizeof(bar), "Brightness", 10, "65%", 3, 0,
        CANOPUS_UI_NODE_FLAG_VISIBLE,
        65, 0, 100, 5, 0
    };
    struct canopus_ui_component_props_v1 image = {
        sizeof(image), "Module icon", 11, "", 0, 0,
        CANOPUS_UI_NODE_FLAG_VISIBLE, 0, 0, 0, 0, 0xCA10u
    };
    struct canopus_ui_component_props_v1 progress = {
        sizeof(progress), "", 0, "", 0, 0,
        CANOPUS_UI_NODE_FLAG_VISIBLE, 250, 0, 1000, 0, 0u
    };
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    const struct canopus_ui_snapshot_v1 *snapshot;

    init_context(&context, &backend, &sink);
    tree = begin_page(&context, "Catalog");
    CHECK(CANOPUS_UI_SCROLL(tree, 2) == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_LIST(tree, 3, "Controls") == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_CHECKBOX(tree, 4, "Telemetry", 200, 1, 1) ==
          CANOPUS_UI_OK);
    CHECK(canopus_ui_component(tree, 5, CANOPUS_UI_NODE_PROGRESS, &bar) ==
          CANOPUS_UI_OK);
    CHECK(canopus_ui_component(tree, 6, CANOPUS_UI_NODE_IMAGE, &image) ==
          CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_PROGRESS(tree, 9, 250, 0, 1000) == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_DIVIDER(tree, 7) == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_SPACER(tree, 8) == CANOPUS_UI_OK);

    style.variant = CANOPUS_UI_VARIANT_TONAL;
    style.foreground = CANOPUS_UI_COLOR_TEXT_PRIMARY;
    style.background = CANOPUS_UI_COLOR_SURFACE_ALT;
    style.accent = CANOPUS_UI_COLOR_ACCENT;
    style.corner_radius = 8;
    style.opacity = 900;
    CHECK(canopus_ui_node_set_style(tree, 5, &style) == CANOPUS_UI_OK);

    layout.width = 240;
    layout.min_height = 44;
    layout.padding_left = 12;
    layout.padding_right = 12;
    layout.axis = CANOPUS_UI_AXIS_HORIZONTAL;
    layout.align = CANOPUS_UI_ALIGN_CENTER;
    layout.justify = CANOPUS_UI_JUSTIFY_SPACE_BETWEEN;
    layout.grow = 1;
    CHECK(canopus_ui_node_set_layout(tree, 5, &layout) == CANOPUS_UI_OK);

    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* list */
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* scroll */
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* page */
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);

    snapshot = canopus_ui_current(&context);
    CHECK(snapshot != 0);
    CHECK(snapshot->nodes[4].type == CANOPUS_UI_NODE_PROGRESS);
    CHECK(snapshot->values[4].value == 65);
    CHECK(snapshot->values[4].maximum == 100);
    CHECK(snapshot->styles[4].variant == CANOPUS_UI_VARIANT_TONAL);
    CHECK(snapshot->styles[4].corner_radius == 8);
    CHECK(snapshot->layouts[4].width == 240);
    CHECK(snapshot->layouts[4].justify == CANOPUS_UI_JUSTIFY_SPACE_BETWEEN);
    CHECK(snapshot->values[5].resource_id == 0xCA10u);
    CHECK(snapshot->nodes[6].type == CANOPUS_UI_NODE_PROGRESS);
    CHECK(snapshot->values[6].value == 250);
    CHECK(snapshot->values[6].maximum == 1000);
    CHECK(activate(&context, snapshot->generation, 4, 200) == 0);
    CHECK(activate(&context, snapshot->generation, 5, 201) ==
          CANOPUS_UI_ERR_ARGUMENT); /* a progress bar has no events */
    CHECK(sink.call_count == 1);
}

TEST(ui_extended_catalog_rejects_invalid_metadata)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_component_props_v1 bar = {
        sizeof(bar), "x", 1, "", 0, 0,
        CANOPUS_UI_NODE_FLAG_VISIBLE, 101, 0, 100, 1, 0
    };
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;

    init_context(&context, &backend, 0);
    tree = begin_page(&context, "Invalid");
    CHECK(canopus_ui_component(tree, 2, CANOPUS_UI_NODE_PROGRESS, &bar) ==
          CANOPUS_UI_ERR_ARGUMENT);
    bar.value = 50;
    CHECK(canopus_ui_component(tree, 2, CANOPUS_UI_NODE_PROGRESS, &bar) ==
          CANOPUS_UI_OK);
    CHECK(canopus_ui_component(tree, 3, 11u, &bar) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_component(tree, 3, CANOPUS_UI_NODE_RETIRED_SLIDER, &bar) ==
          CANOPUS_UI_ERR_ARGUMENT);
    style.opacity = 1001;
    CHECK(canopus_ui_node_set_style(tree, 2, &style) == CANOPUS_UI_ERR_ARGUMENT);
    layout.width = -2;
    CHECK(canopus_ui_node_set_layout(tree, 2, &layout) == CANOPUS_UI_ERR_ARGUMENT);
    canopus_ui_tree_abort(tree);
}

TEST(ui_requires_explicit_backend_capabilities)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend state = {0};
    struct canopus_ui_backend_v1 backend = fake_backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    struct canopus_ui_layout_v1 layout = CANOPUS_UI_LAYOUT_DEFAULT;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;

    backend.abi_major = 1u;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) ==
          CANOPUS_UI_ERR_ARGUMENT);
    backend.abi_major = CANOPUS_UI_ABI_MAJOR;
    backend.abi_minor = 1u;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) ==
          CANOPUS_UI_ERR_ARGUMENT);
    backend.abi_minor = CANOPUS_UI_ABI_MINOR;
    backend.control = 0;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) ==
          CANOPUS_UI_ERR_ARGUMENT);
    backend.capabilities = 0u;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) ==
          CANOPUS_UI_OK);
    CHECK(canopus_ui_context_capabilities(&context) == 0u);
    tree = begin_page(&context, "Explicit");
    CHECK(CANOPUS_UI_TEXT(tree, 2, "Title", CANOPUS_UI_TEXT_TITLE) ==
          CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(CANOPUS_UI_DIVIDER(tree, 2) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(CANOPUS_UI_NAVIGATION_HEADER(tree, 2, "Header", "", 1, 1) ==
          CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_node_set_style(tree, 1, &style) == CANOPUS_UI_OK);
    CHECK(canopus_ui_node_set_layout(tree, 1, &layout) == CANOPUS_UI_OK);
    CHECK(canopus_ui_node_set_visual(tree, 1, &visual) == CANOPUS_UI_OK);
    style.variant = CANOPUS_UI_VARIANT_TONAL;
    layout.width = 240;
    visual.flags = CANOPUS_UI_VISUAL_FLAG_CLIP;
    CHECK(canopus_ui_node_set_style(tree, 1, &style) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_node_set_layout(tree, 1, &layout) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_node_set_visual(tree, 1, &visual) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    CHECK(state.applied.abi_major == 2u && state.applied.abi_minor == 0u);
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_CAPTURE,
                             0, 0, 0) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(state.control_count == 0u);
    {
        const struct canopus_ui_event_v1 event = {
            .struct_size = sizeof(event), .generation = 1, .key = 1,
            .type = CANOPUS_UI_EVENT_PRESS
        };
        CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_ERR_UNSUPPORTED);
    }

    backend = fake_backend;
    backend.capabilities &= ~(CANOPUS_UI_CAP_IMAGE | CANOPUS_UI_CAP_PROGRESS);
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) == CANOPUS_UI_OK);
    tree = begin_page(&context, "Selective");
    CHECK(CANOPUS_UI_IMAGE(tree, 2, "Icon", 10) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(CANOPUS_UI_PROGRESS(tree, 2, 25, 0, 100) == CANOPUS_UI_ERR_UNSUPPORTED);
    canopus_ui_tree_abort(tree);

    init_context(&context, &state, 0);
    CHECK(canopus_ui_context_capabilities(&context) == FAKE_CAPABILITIES);
}

TEST(ui_navigation_header_and_router_are_bounded)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend backend;
    struct fake_event_sink sink;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_navigation_header_props_v1 header = {
        sizeof(header), "Modules", 7, "2 installed", 11,
        301, 1, 1, 0
    };
    struct canopus_ui_router_v1 router;
    const struct canopus_ui_snapshot_v1 *snapshot;
    uint32_t route = 0;
    uint32_t stale_generation;
    uint32_t i;

    init_context(&context, &backend, &sink);
    tree = begin_page(&context, "Canopus");
    CHECK(canopus_ui_navigation_header(tree, 2, &header) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* header */
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* page */
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&context);
    CHECK(snapshot != 0);
    CHECK(snapshot->nodes[1].type == CANOPUS_UI_NODE_NAVIGATION_HEADER);
    CHECK((snapshot->nodes[1].flags & CANOPUS_UI_NODE_FLAG_HEADER_BACK) != 0u);
    CHECK((snapshot->nodes[1].flags & CANOPUS_UI_NODE_FLAG_HEADER_CENTERED) != 0u);
    CHECK(activate(&context, snapshot->generation, 2, 301) == 0);

    /* A hidden back keeps its reserved id; only a visible one needs a handler. */
    header.show_back = 0;
    tree = begin_page(&context, "Root");
    CHECK(canopus_ui_navigation_header(tree, 2, &header) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* header */
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK); /* page */
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&context);
    CHECK((snapshot->nodes[1].flags & CANOPUS_UI_NODE_FLAG_HEADER_BACK) == 0u);
    CHECK(snapshot->nodes[1].event_id == 301);

    CHECK(canopus_ui_router_init(&router, 10) == CANOPUS_UI_OK);
    CHECK(canopus_ui_router_current(&router) == 10u);
    CHECK(canopus_ui_router_depth(&router) == 1u);
    CHECK(canopus_ui_router_pop(&router, &route) == CANOPUS_UI_ERR_STATE);
    stale_generation = router.generation;
    CHECK(canopus_ui_router_push(&router, 20) == CANOPUS_UI_OK);
    CHECK(canopus_ui_router_back(&router, stale_generation, &route) ==
          CANOPUS_UI_ERR_STALE);
    CHECK(canopus_ui_router_back(&router, router.generation, &route) ==
          CANOPUS_UI_OK);
    CHECK(route == 10u);
    CHECK(canopus_ui_router_replace(&router, 11) == CANOPUS_UI_OK);
    CHECK(canopus_ui_router_current(&router) == 11u);
    for (i = 1u; i < CANOPUS_UI_ROUTER_MAX_DEPTH; i++) {
        CHECK(canopus_ui_router_push(&router, 100u + i) == CANOPUS_UI_OK);
    }
    CHECK(canopus_ui_router_push(&router, 999) == CANOPUS_UI_ERR_CAPACITY);
    CHECK(canopus_ui_router_pop_to(&router, 103) == CANOPUS_UI_OK);
    CHECK(canopus_ui_router_current(&router) == 103u);
    CHECK(canopus_ui_router_clear_to_root(&router) == CANOPUS_UI_OK);
    CHECK(canopus_ui_router_depth(&router) == 1u);
}

TEST(ui_abi_2_metadata_layout)
{
    const size_t prefix = 12u +
        CANOPUS_UI_MAX_NODES * sizeof(struct canopus_ui_node_v1) +
        CANOPUS_UI_STRING_CAPACITY;

    CHECK(sizeof(struct canopus_ui_node_v1) == 28u);
    CHECK(sizeof(struct canopus_ui_style_v1) == 20u);
    CHECK(sizeof(struct canopus_ui_layout_v1) == 38u);
    CHECK(sizeof(struct canopus_ui_value_v1) == 20u);
    CHECK(offsetof(struct canopus_ui_snapshot_v1, nodes) == 12u);
    CHECK(offsetof(struct canopus_ui_snapshot_v1, strings) ==
          12u + CANOPUS_UI_MAX_NODES * sizeof(struct canopus_ui_node_v1));
    CHECK(offsetof(struct canopus_ui_snapshot_v1, styles) == prefix);
    CHECK(offsetof(struct canopus_ui_snapshot_v1, layouts) ==
          prefix + CANOPUS_UI_MAX_NODES * sizeof(struct canopus_ui_style_v1));
    CHECK(offsetof(struct canopus_ui_snapshot_v1, values) ==
          prefix + CANOPUS_UI_MAX_NODES *
          (sizeof(struct canopus_ui_style_v1) +
           sizeof(struct canopus_ui_layout_v1)));
    CHECK(sizeof(struct canopus_ui_visual_v1) == 36u);
    CHECK(sizeof(struct canopus_ui_event_v1) == 52u);
    CHECK(sizeof(struct canopus_ui_scroll_metrics_v1) == 12u);
    CHECK(offsetof(struct canopus_ui_snapshot_v1, visuals) == 4940u);
    CHECK(sizeof(struct canopus_ui_snapshot_v1) == 6092u);
}

TEST(ui_visual_metadata_is_transactional_and_capability_checked)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend state;
    struct canopus_ui_backend_v1 backend = fake_backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    struct canopus_ui_style_v1 style = CANOPUS_UI_STYLE_DEFAULT;
    const struct canopus_ui_snapshot_v1 *snapshot;

    init_context(&context, &state, 0);
    tree = begin_page(&context, "Visuals");
    CHECK(CANOPUS_UI_IMAGE(tree, 2, "Icon", 10) == CANOPUS_UI_OK);
    visual.flags = CANOPUS_UI_VISUAL_FLAG_FOREGROUND_RGB |
        CANOPUS_UI_VISUAL_FLAG_BACKGROUND_RGB | CANOPUS_UI_VISUAL_FLAG_BORDER_RGB |
        CANOPUS_UI_VISUAL_FLAG_ABSOLUTE | CANOPUS_UI_VISUAL_FLAG_OPACITY |
        CANOPUS_UI_VISUAL_FLAG_CLIP;
    visual.foreground_rgb = 0xABCDEFu;
    visual.background_rgb = 0u; /* explicit black */
    visual.border_rgb = 0x123456u;
    visual.x = -3;
    visual.y = 42;
    visual.translate_x = 4;
    visual.font_px = 22;
    visual.image_fit = CANOPUS_UI_IMAGE_FIT_CONTAIN;
    CHECK(canopus_ui_node_set_style(tree, 2, &style) == CANOPUS_UI_OK);
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_OK);
    visual.image_fit = CANOPUS_UI_IMAGE_FIT_STRETCH + 1u;
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(tree->_snapshot.visuals[1].image_fit == CANOPUS_UI_IMAGE_FIT_CONTAIN);
    visual.image_fit = CANOPUS_UI_IMAGE_FIT_NATIVE;
    visual.flags |= CANOPUS_UI_VISUAL_FLAG_VIRTUAL_CONTENT;
    visual.content_height = 10000u;
    visual.scroll_y = 400;
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_node_set_visual(tree, 1, &visual) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    snapshot = canopus_ui_current(&context);
    CHECK(snapshot->visuals[0].content_height == 10000u);
    CHECK(snapshot->visuals[0].scroll_y == 400);
    CHECK(snapshot->visuals[1].foreground_rgb == 0xABCDEFu);
    CHECK(snapshot->styles[1].opacity == 0u);
    CHECK((snapshot->visuals[1].flags & CANOPUS_UI_VISUAL_FLAG_OPACITY) != 0u);

    state.fail = CANOPUS_UI_ERR_UNSUPPORTED;
    tree = begin_page(&context, "Rejected");
    CHECK(canopus_ui_node_set_visual(tree, 1, &visual) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_current(&context)->generation == 1u);
    CHECK(canopus_ui_current(&context)->visuals[0].scroll_y == 400);

    backend.capabilities &= ~CANOPUS_UI_CAP_IMAGE_FIT_COVER;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) == CANOPUS_UI_OK);
    tree = begin_page(&context, "Fit");
    CHECK(CANOPUS_UI_IMAGE(tree, 2, "Icon", 10) == CANOPUS_UI_OK);
    memset(&visual, 0, sizeof(visual));
    visual.image_fit = CANOPUS_UI_IMAGE_FIT_COVER;
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(tree->_snapshot.visuals[1].image_fit == CANOPUS_UI_IMAGE_FIT_NATIVE);
    visual.image_fit = CANOPUS_UI_IMAGE_FIT_NATIVE;
    visual.x = 1;
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_ERR_ARGUMENT);
    canopus_ui_tree_abort(tree);
}

TEST(ui_font_sizes_require_the_advertised_capability)
{
    static const uint16_t fixed_sizes[] = {0, 24, 28};
    static const uint16_t arbitrary_sizes[] = {1, 21, 26, 32, UINT16_MAX};
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend state;
    struct canopus_ui_backend_v1 backend = fake_backend;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_visual_v1 visual = CANOPUS_UI_VISUAL_DEFAULT;
    uint32_t i;

    backend.capabilities &= ~CANOPUS_UI_CAP_FONT;
    CHECK(canopus_ui_context_init(&context, &backend, &state, 0, 0) == CANOPUS_UI_OK);
    memset(&state, 0, sizeof(state));
    CHECK((canopus_ui_context_capabilities(&context) & CANOPUS_UI_CAP_FONT) == 0u);
    tree = begin_page(&context, "Fixed fonts");
    CHECK(CANOPUS_UI_TEXT(tree, 2, "Label", CANOPUS_UI_TEXT_BODY) == CANOPUS_UI_OK);
    for (i = 0; i < sizeof(fixed_sizes) / sizeof(fixed_sizes[0]); i++) {
        visual.font_px = fixed_sizes[i];
        CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_OK);
        CHECK(tree->_snapshot.visuals[1].font_px == fixed_sizes[i]);
    }
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_current(&context)->visuals[1].font_px == 28u);

    tree = begin_page(&context, "Rejected fonts");
    CHECK(CANOPUS_UI_TEXT(tree, 2, "Label", CANOPUS_UI_TEXT_BODY) == CANOPUS_UI_OK);
    visual.font_px = 24;
    CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_OK);
    for (i = 0; i < sizeof(arbitrary_sizes) / sizeof(arbitrary_sizes[0]); i++) {
        visual.font_px = arbitrary_sizes[i];
        CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_ERR_UNSUPPORTED);
        CHECK(tree->_snapshot.visuals[1].font_px == 24u);
        CHECK(canopus_ui_current(&context)->visuals[1].font_px == 28u);
        CHECK(state.apply_count == 1u);
    }
    canopus_ui_tree_abort(tree);

    backend.capabilities |= CANOPUS_UI_CAP_FONT;
    CHECK((canopus_ui_context_capabilities(&context) & CANOPUS_UI_CAP_FONT) != 0u);
    tree = begin_page(&context, "Arbitrary fonts");
    CHECK(CANOPUS_UI_TEXT(tree, 2, "Label", CANOPUS_UI_TEXT_BODY) == CANOPUS_UI_OK);
    for (i = 0; i < sizeof(arbitrary_sizes) / sizeof(arbitrary_sizes[0]); i++) {
        visual.font_px = arbitrary_sizes[i];
        CHECK(canopus_ui_node_set_visual(tree, 2, &visual) == CANOPUS_UI_OK);
        CHECK(tree->_snapshot.visuals[1].font_px == arbitrary_sizes[i]);
    }
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_current(&context)->visuals[1].font_px == UINT16_MAX);
    CHECK(state.applied.visuals[1].font_px == UINT16_MAX);
    CHECK(state.apply_count == 2u);
}

TEST(ui_pointer_scroll_events_and_controls_use_committed_generation)
{
    struct canopus_ui_context_v1 context;
    struct fake_ui_backend state;
    struct fake_event_sink sink;
    struct canopus_ui_tree_v1 *tree;
    struct canopus_ui_scroll_metrics_v1 metrics = {0};
    struct canopus_ui_event_v1 event = {
        .struct_size = sizeof(event), .generation = 1, .key = 1,
        .type = CANOPUS_UI_EVENT_PRESS, .time_ms = 123,
        .x = 4, .y = 9, .delta_x = -2, .delta_y = 5
    };
    uint32_t op;
    init_context(&context, &state, &sink);
    tree = begin_page(&context, "Input");
    CHECK(CANOPUS_UI_BUTTON(tree, 2, "Disabled", 100, 0) == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_END(tree) == CANOPUS_UI_OK);
    CHECK(CANOPUS_UI_COMMIT(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_OK);
    CHECK(memcmp(&sink.event, &event, sizeof(event)) == 0);
    event.type = CANOPUS_UI_EVENT_SCROLL;
    event.scroll_y = 40;
    event.viewport_height = 100;
    event.content_height = 500;
    CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_OK);
    CHECK(sink.event.scroll_y == 40 && sink.event.content_height == 500);
    event.key = 2;
    event.event_id = 100;
    event.type = CANOPUS_UI_EVENT_MOVE;
    CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_ERR_DISABLED);
    event.struct_size--;
    CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_ERR_ARGUMENT);
    event.struct_size++;
    event.generation = 2;
    CHECK(canopus_ui_dispatch_event(&context, &event) == CANOPUS_UI_ERR_STALE);
    CHECK(context.dropped_events == 1u);

    CHECK(canopus_ui_control(&context, 0, 1, CANOPUS_UI_CONTROL_SCROLL_QUERY,
                             0, 0, &metrics) == CANOPUS_UI_ERR_STALE);
    CHECK(canopus_ui_control(&context, 1, 999, CANOPUS_UI_CONTROL_CAPTURE,
                             0, 0, 0) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_control(&context, 1, 2, CANOPUS_UI_CONTROL_CAPTURE,
                             0, 0, 0) == CANOPUS_UI_ERR_DISABLED);
    CHECK(canopus_ui_control(&context, 1, 2, CANOPUS_UI_CONTROL_SCROLL_QUERY,
                             0, 0, &metrics) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_SCROLL_TO,
                             -1, 0, 0) == CANOPUS_UI_ERR_ARGUMENT);
    CHECK(state.control_count == 0u);
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_SCROLL_QUERY,
                             0, 0, &metrics) == CANOPUS_UI_OK);
    CHECK(metrics.y == 40 && metrics.viewport_height == 100 && metrics.content_height == 500);
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_SCROLL_TO,
                             300, 0, 0) == CANOPUS_UI_OK);
    CHECK(state.control_a == 300);
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_TRANSLATE,
                             -20, 15, 0) == CANOPUS_UI_OK);
    CHECK(state.control_a == -20 && state.control_b == 15);
    for (op = CANOPUS_UI_CONTROL_CAPTURE; op <= CANOPUS_UI_CONTROL_RELEASE_CAPTURE; op++) {
        CHECK(canopus_ui_control(&context, 1, 1, op, 0, 0, 0) == CANOPUS_UI_OK);
    }
    CHECK(state.control_count == 5u && state.control_generation == 1u && state.control_key == 1u);
    state.control_result = CANOPUS_UI_ERR_UNSUPPORTED;
    CHECK(canopus_ui_control(&context, 1, 1, CANOPUS_UI_CONTROL_SCROLL_TO,
                             0, 0, 0) == CANOPUS_UI_ERR_UNSUPPORTED);
    CHECK(canopus_ui_current(&context)->generation == 1u);
}

static const struct test_registry ui_tests[] = {
    { "ui_builds_linked_tree_and_copies_strings",
      ui_builds_linked_tree_and_copies_strings_wrapper },
    { "ui_rejects_invalid_shape_and_duplicate_keys",
      ui_rejects_invalid_shape_and_duplicate_keys_wrapper },
    { "ui_context_drops_a_tree_its_owner_left_open",
      ui_context_drops_a_tree_its_owner_left_open_wrapper },
    { "ui_enforces_node_and_depth_capacity",
      ui_enforces_node_and_depth_capacity_wrapper },
    { "ui_string_failure_rolls_back_entire_node",
      ui_string_failure_rolls_back_entire_node_wrapper },
    { "ui_backend_failure_preserves_previous_commit",
      ui_backend_failure_preserves_previous_commit_wrapper },
    { "ui_events_are_generation_checked_and_interactive_only",
      ui_events_are_generation_checked_and_interactive_only_wrapper },
    { "ui_validates_backend_and_property_headers",
      ui_validates_backend_and_property_headers_wrapper },
    { "ui_extended_catalog_supports_style_layout_and_values",
      ui_extended_catalog_supports_style_layout_and_values_wrapper },
    { "ui_extended_catalog_rejects_invalid_metadata",
      ui_extended_catalog_rejects_invalid_metadata_wrapper },
    { "ui_requires_explicit_backend_capabilities",
      ui_requires_explicit_backend_capabilities_wrapper },
    { "ui_navigation_header_and_router_are_bounded",
      ui_navigation_header_and_router_are_bounded_wrapper },
    { "ui_abi_2_metadata_layout",
      ui_abi_2_metadata_layout_wrapper },
    { "ui_visual_metadata_is_transactional_and_capability_checked",
      ui_visual_metadata_is_transactional_and_capability_checked_wrapper },
    { "ui_font_sizes_require_the_advertised_capability",
      ui_font_sizes_require_the_advertised_capability_wrapper },
    { "ui_pointer_scroll_events_and_controls_use_committed_generation",
      ui_pointer_scroll_events_and_controls_use_committed_generation_wrapper },
};

int run_ui_tests(void)
{
    RUN_TESTS(ui_tests, sizeof(ui_tests) / sizeof(ui_tests[0]));
}
