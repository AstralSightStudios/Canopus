/* Host tests: P65 semantic snapshot-to-row adapter. */
#include "canopus_test.h"
#include "canopus_manager_p65_rows.h"
#include <string.h>

static uint16_t append_string(struct canopus_ui_snapshot_v1 *snapshot,
                              const char *text)
{
    uint16_t offset = snapshot->string_used;
    size_t length = strlen(text);

    CHECK(length < CANOPUS_UI_STRING_CAPACITY - snapshot->string_used);
    if (length >= CANOPUS_UI_STRING_CAPACITY - snapshot->string_used) {
        return offset;
    }
    memcpy(snapshot->strings + offset, text, length + 1u);
    snapshot->string_used = (uint16_t)(snapshot->string_used + length + 1u);
    return offset;
}

static void set_node(struct canopus_ui_snapshot_v1 *snapshot, uint16_t index,
                     uint16_t type, canopus_ui_node_id key, uint16_t parent,
                     uint32_t flags, const char *primary,
                     const char *secondary, uint32_t event_id)
{
    struct canopus_ui_node_v1 *node = &snapshot->nodes[index];

    memset(node, 0, sizeof(*node));
    node->key = key;
    node->type = type;
    node->parent = parent;
    node->first_child = CANOPUS_UI_NO_NODE;
    node->next_sibling = CANOPUS_UI_NO_NODE;
    node->primary_off = append_string(snapshot, primary);
    node->primary_len = (uint16_t)strlen(primary);
    node->secondary_off = append_string(snapshot, secondary);
    node->secondary_len = (uint16_t)strlen(secondary);
    node->event_id = event_id;
    node->flags = flags;
}

TEST(p65_rows_flatten_visible_nodes_and_keep_descendants)
{
    struct canopus_ui_snapshot_v1 snapshot;
    struct canopus_p65_ui_rows_v1 rows;
    struct canopus_p65_ui_row_view_v1 view;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.abi_major = CANOPUS_UI_ABI_MAJOR;
    snapshot.abi_minor = CANOPUS_UI_ABI_MINOR;
    snapshot.generation = 7u;
    snapshot.node_count = 5u;
    set_node(&snapshot, 0u, CANOPUS_UI_NODE_NAVIGATION_PAGE, 1u,
             CANOPUS_UI_NO_NODE, CANOPUS_UI_NODE_FLAG_VISIBLE,
             "Canopus Manager", "", 0u);
    set_node(&snapshot, 1u, CANOPUS_UI_NODE_SECTION, 2u, 0u,
             CANOPUS_UI_NODE_FLAG_VISIBLE, "Modules", "", 0u);
    set_node(&snapshot, 2u, CANOPUS_UI_NODE_STATUS_ROW, 3u, 1u,
             CANOPUS_UI_NODE_FLAG_VISIBLE, "Active", "2", 0u);
    set_node(&snapshot, 3u, CANOPUS_UI_NODE_LIST, 4u, 1u,
             CANOPUS_UI_NODE_FLAG_VISIBLE, "", "", 0u);
    set_node(&snapshot, 4u, CANOPUS_UI_NODE_ACTION_ROW, 5u, 3u,
             CANOPUS_UI_NODE_FLAG_VISIBLE | CANOPUS_UI_NODE_FLAG_ENABLED,
             "Install", "Ready", 42u);

    CHECK(canopus_p65_ui_rows_build(&rows, &snapshot) == CANOPUS_UI_OK);
    CHECK_EQ(rows.count, 3u);
    CHECK_EQ(rows.generation, 7u);
    /* The backend receives a staging snapshot; the adapter must own its copy. */
    snapshot.generation = 99u;
    snapshot.strings[snapshot.nodes[1].primary_off] = 'X';
    CHECK(canopus_p65_ui_rows_get(&rows, 0u, &view) == CANOPUS_UI_OK);
    CHECK_EQ(view.node->type, CANOPUS_UI_NODE_SECTION);
    CHECK(strcmp(view.primary, "Modules") == 0);
    CHECK_EQ(view.event_id, 0u);
    CHECK(canopus_p65_ui_rows_get(&rows, 1u, &view) == CANOPUS_UI_OK);
    CHECK_EQ(view.node->type, CANOPUS_UI_NODE_STATUS_ROW);
    CHECK(strcmp(view.primary, "Active") == 0);
    CHECK(strcmp(view.secondary, "2") == 0);
    CHECK(canopus_p65_ui_rows_get(&rows, 2u, &view) == CANOPUS_UI_OK);
    CHECK_EQ(view.node->key, 5u);
    CHECK_EQ(view.event_id, 42u);
    CHECK_EQ(view.generation, 7u);
    CHECK_EQ(view.cache_key, CANOPUS_UI_NODE_ACTION_ROW);
    CHECK(strcmp(view.secondary, "Ready") == 0);
}

TEST(p65_rows_respect_hidden_ancestors_and_disabled_events)
{
    struct canopus_ui_snapshot_v1 snapshot;
    struct canopus_p65_ui_rows_v1 rows;
    struct canopus_p65_ui_row_view_v1 view;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.abi_major = CANOPUS_UI_ABI_MAJOR;
    snapshot.abi_minor = CANOPUS_UI_ABI_MINOR;
    snapshot.generation = 2u;
    snapshot.node_count = 3u;
    set_node(&snapshot, 0u, CANOPUS_UI_NODE_NAVIGATION_PAGE, 1u,
             CANOPUS_UI_NO_NODE, CANOPUS_UI_NODE_FLAG_VISIBLE,
             "Page", "", 0u);
    set_node(&snapshot, 1u, CANOPUS_UI_NODE_LIST, 2u, 0u, 0u,
             "", "", 0u);
    set_node(&snapshot, 2u, CANOPUS_UI_NODE_BUTTON, 3u, 1u,
             CANOPUS_UI_NODE_FLAG_VISIBLE, "Disabled", "", 77u);

    CHECK(canopus_p65_ui_rows_build(&rows, &snapshot) == CANOPUS_UI_OK);
    CHECK_EQ(rows.count, 0u);

    snapshot.nodes[1].flags = CANOPUS_UI_NODE_FLAG_VISIBLE;
    CHECK(canopus_p65_ui_rows_build(&rows, &snapshot) == CANOPUS_UI_OK);
    CHECK_EQ(rows.count, 1u);
    CHECK(canopus_p65_ui_rows_get(&rows, 0u, &view) == CANOPUS_UI_OK);
    CHECK_EQ(view.node->key, 3u);
    CHECK_EQ(view.event_id, 0u);
}

TEST(p65_rows_reject_bad_strings_without_overwriting_output)
{
    struct canopus_ui_snapshot_v1 snapshot;
    struct canopus_p65_ui_rows_v1 rows;

    memset(&snapshot, 0, sizeof(snapshot));
    snapshot.abi_major = CANOPUS_UI_ABI_MAJOR;
    snapshot.abi_minor = CANOPUS_UI_ABI_MINOR;
    snapshot.node_count = 2u;
    snapshot.string_used = 2u;
    set_node(&snapshot, 0u, CANOPUS_UI_NODE_NAVIGATION_PAGE, 1u,
             CANOPUS_UI_NO_NODE, CANOPUS_UI_NODE_FLAG_VISIBLE,
             "", "", 0u);
    snapshot.nodes[1].key = 2u;
    snapshot.nodes[1].type = CANOPUS_UI_NODE_TEXT;
    snapshot.nodes[1].parent = 0u;
    snapshot.nodes[1].flags = CANOPUS_UI_NODE_FLAG_VISIBLE;
    snapshot.nodes[1].primary_off = 3u;
    snapshot.nodes[1].primary_len = 2u;
    snapshot.nodes[1].secondary_off = 0u;
    snapshot.nodes[1].secondary_len = 0u;

    memset(&rows, 0, sizeof(rows));
    rows.generation = 99u;
    rows.count = 99u;
    CHECK(canopus_p65_ui_rows_build(&rows, &snapshot) ==
          CANOPUS_UI_ERR_STATE);
    CHECK_EQ(rows.generation, 99u);
    CHECK_EQ(rows.count, 99u);
}

struct p65_event_sink {
    uint32_t calls;
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
};

static int32_t p65_fake_apply(void *cookie,
                              const struct canopus_ui_snapshot_v1 *snapshot)
{
    uint32_t *calls = (uint32_t *)cookie;
    (void)snapshot;
    (*calls)++;
    return CANOPUS_UI_OK;
}

static int32_t p65_fake_event(void *cookie, uint32_t generation,
                              canopus_ui_node_id key, uint32_t event_id)
{
    struct p65_event_sink *sink = (struct p65_event_sink *)cookie;
    sink->calls++;
    sink->generation = generation;
    sink->key = key;
    sink->event_id = event_id;
    return CANOPUS_UI_OK;
}

TEST(p65_row_click_dispatches_generation_checked_event)
{
    struct canopus_ui_context_v1 ui;
    struct canopus_ui_backend_v1 backend_api = {
        sizeof(struct canopus_ui_backend_v1), CANOPUS_UI_ABI_MAJOR,
        CANOPUS_UI_ABI_MINOR, p65_fake_apply
    };
    struct canopus_p65_ui_rows_v1 rows;
    struct canopus_p65_ui_row_view_v1 view;
    struct canopus_p65_ui_event_binding_v1 binding;
    struct p65_event_sink sink = {0};
    struct canopus_ui_tree_v1 *tree = 0;
    struct canopus_ui_navigation_page_props_v1 page = {
        sizeof(page), "Manager", 7u
    };
    struct canopus_ui_button_props_v1 button = {
        sizeof(button), "Install", 7u, 42u, 1u
    };
    uint8_t event[12u + sizeof(uintptr_t)] = {0};
    uintptr_t user_data;
    uint32_t applies = 0u;
    uint32_t i;

    backend_api.apply = p65_fake_apply;
    CHECK(canopus_ui_context_init(&ui, &backend_api, &applies,
                                  p65_fake_event, &sink) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_begin(&ui, &tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_navigation_page(tree, 1u, &page) == CANOPUS_UI_OK);
    CHECK(canopus_ui_button(tree, 9u, &button) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    CHECK(canopus_p65_ui_rows_build(&rows, canopus_ui_current(&ui)) ==
          CANOPUS_UI_OK);
    CHECK_EQ(rows.count, 1u);
    CHECK(canopus_p65_ui_rows_get(&rows, 0u, &view) == CANOPUS_UI_OK);
    CHECK(canopus_p65_ui_event_binding_init(&binding, &ui, &view) ==
          CANOPUS_UI_OK);

    event[8] = 6u; /* Non-click events must not reach Manager dispatch. */
    user_data = (uintptr_t)&binding;
    for (i = 0u; i < sizeof(user_data); i++) {
        event[12u + i] = (uint8_t)(user_data >> (i * 8u));
    }
    canopus_p65_ui_row_click_event(event);
    CHECK_EQ(sink.calls, 0u);
    event[8] = 7u; /* P65 click event code at event + 8 */
    canopus_p65_ui_row_click_event(event);
    CHECK_EQ(sink.calls, 1u);
    CHECK_EQ(sink.generation, 1u);
    CHECK_EQ(sink.key, 9u);
    CHECK_EQ(sink.event_id, 42u);

    /* The old firmware row binding cannot dispatch against a newer commit. */
    tree = 0;
    CHECK(canopus_ui_tree_begin(&ui, &tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_navigation_page(tree, 1u, &page) == CANOPUS_UI_OK);
    CHECK(canopus_ui_button(tree, 9u, &button) == CANOPUS_UI_OK);
    CHECK(canopus_ui_end(tree) == CANOPUS_UI_OK);
    CHECK(canopus_ui_tree_commit(tree) == CANOPUS_UI_OK);
    canopus_p65_ui_row_click_event(event);
    CHECK_EQ(sink.calls, 1u);
    CHECK_EQ(applies, 2u);
}

static const struct test_registry p65_ui_rows_tests[] = {
    { "p65_rows_flatten_visible_nodes_and_keep_descendants",
      p65_rows_flatten_visible_nodes_and_keep_descendants_wrapper },
    { "p65_rows_respect_hidden_ancestors_and_disabled_events",
      p65_rows_respect_hidden_ancestors_and_disabled_events_wrapper },
    { "p65_rows_reject_bad_strings_without_overwriting_output",
      p65_rows_reject_bad_strings_without_overwriting_output_wrapper },
    { "p65_row_click_dispatches_generation_checked_event",
      p65_row_click_dispatches_generation_checked_event_wrapper },
};

int run_p65_ui_rows_tests(void)
{
    RUN_TESTS(p65_ui_rows_tests,
              sizeof(p65_ui_rows_tests) / sizeof(p65_ui_rows_tests[0]));
}
