/* Host tests: bounded semantic native UI tree. */
#include "canopus_test.h"
#include "canopus_ui.h"
#include <string.h>

struct fake_ui_backend {
    struct canopus_ui_snapshot_v1 applied;
    uint32_t apply_count;
    int fail;
};

struct fake_event_sink {
    uint32_t call_count;
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
    int32_t result;
};

static int32_t fake_apply(void *cookie,
                          const struct canopus_ui_snapshot_v1 *snapshot)
{
    struct fake_ui_backend *fake = (struct fake_ui_backend *)cookie;
    fake->apply_count++;
    if (fake->fail) {
        return -1;
    }
    fake->applied = *snapshot;
    return 0;
}

static int32_t fake_event(void *cookie, uint32_t generation,
                          canopus_ui_node_id key, uint32_t event_id)
{
    struct fake_event_sink *sink = (struct fake_event_sink *)cookie;
    sink->call_count++;
    sink->generation = generation;
    sink->key = key;
    sink->event_id = event_id;
    return sink->result;
}

static const struct canopus_ui_backend_v1 fake_backend = {
    sizeof(struct canopus_ui_backend_v1),
    CANOPUS_UI_ABI_MAJOR,
    CANOPUS_UI_ABI_MINOR,
    fake_apply,
};

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
    CHECK(canopus_ui_dispatch_event(&context, 0, 2, 77) ==
          CANOPUS_UI_ERR_STALE);
    CHECK(context.dropped_events == 1);
    CHECK(sink.call_count == 0);
    CHECK(canopus_ui_dispatch_event(&context, 1, 3, 88) ==
          CANOPUS_UI_ERR_DISABLED);
    CHECK(canopus_ui_dispatch_event(&context, 1, 4, 77) ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_dispatch_event(&context, 1, 2, 78) ==
          CANOPUS_UI_ERR_ARGUMENT);
    CHECK(canopus_ui_dispatch_event(&context, 1, 5, 99) == 19);
    CHECK(canopus_ui_dispatch_event(&context, 1, 2, 77) == 19);
    CHECK(sink.call_count == 2);
    CHECK(sink.generation == 1);
    CHECK(sink.key == 2);
    CHECK(sink.event_id == 77);
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

static const struct test_registry ui_tests[] = {
    { "ui_builds_linked_tree_and_copies_strings",
      ui_builds_linked_tree_and_copies_strings_wrapper },
    { "ui_rejects_invalid_shape_and_duplicate_keys",
      ui_rejects_invalid_shape_and_duplicate_keys_wrapper },
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
};

int run_ui_tests(void)
{
    RUN_TESTS(ui_tests, sizeof(ui_tests) / sizeof(ui_tests[0]));
}
