/*
 * canopus_ui.h — bounded semantic native UI tree for C applications.
 *
 * Applications describe system-semantic components. A target backend maps the
 * committed snapshot to exact-firmware prefab objects; public code never owns a
 * firmware widget pointer. Trees and strings are fixed-capacity and commit is
 * transactional: a rejected staging tree leaves the previous generation live.
 */
#ifndef CANOPUS_UI_H
#define CANOPUS_UI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_UI_ABI_MAJOR 1u
#define CANOPUS_UI_ABI_MINOR 0u

#define CANOPUS_UI_MAX_NODES        32u
#define CANOPUS_UI_MAX_DEPTH         8u
#define CANOPUS_UI_STRING_CAPACITY 768u
#define CANOPUS_UI_NO_NODE          0xFFFFu

#define CANOPUS_UI_OK             0
#define CANOPUS_UI_ERR_ARGUMENT  -1
#define CANOPUS_UI_ERR_STATE     -2
#define CANOPUS_UI_ERR_CAPACITY  -3
#define CANOPUS_UI_ERR_DUP_KEY   -4
#define CANOPUS_UI_ERR_BACKEND   -5
#define CANOPUS_UI_ERR_STALE     -6
#define CANOPUS_UI_ERR_DISABLED  -7

typedef uint32_t canopus_ui_node_id;

enum canopus_ui_node_type {
    CANOPUS_UI_NODE_NAVIGATION_PAGE = 1,
    CANOPUS_UI_NODE_SECTION,
    CANOPUS_UI_NODE_TEXT,
    CANOPUS_UI_NODE_STATUS_ROW,
    CANOPUS_UI_NODE_BUTTON,
};

#define CANOPUS_UI_NODE_FLAG_ENABLED (1u << 0)

/* A node references strings copied into its tree's bounded string arena. */
struct canopus_ui_node_v1 {
    canopus_ui_node_id key;
    uint16_t type;
    uint16_t parent;
    uint16_t first_child;
    uint16_t next_sibling;
    uint16_t primary_off;
    uint16_t primary_len;
    uint16_t secondary_off;
    uint16_t secondary_len;
    uint32_t event_id;
    uint32_t flags;
};

struct canopus_ui_snapshot_v1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t generation;
    uint16_t node_count;
    uint16_t string_used;
    struct canopus_ui_node_v1 nodes[CANOPUS_UI_MAX_NODES];
    char strings[CANOPUS_UI_STRING_CAPACITY];
};

struct canopus_ui_context_v1;
struct canopus_ui_tree_v1;

/* The backend must either apply the complete snapshot and return 0, or leave
 * its previously rendered native tree intact and return a negative value. */
typedef int32_t (*canopus_ui_backend_apply_v1)(
    void *cookie, const struct canopus_ui_snapshot_v1 *snapshot);

typedef int32_t (*canopus_ui_event_handler_v1)(
    void *cookie, uint32_t generation, canopus_ui_node_id key,
    uint32_t event_id);

struct canopus_ui_backend_v1 {
    uint32_t struct_size;
    uint16_t abi_major;
    uint16_t abi_minor;
    canopus_ui_backend_apply_v1 apply;
};

/* Concrete storage is public so freestanding callers can allocate it without
 * a heap. Fields prefixed with '_' are runtime-owned and must not be modified. */
struct canopus_ui_tree_v1 {
    struct canopus_ui_context_v1 *_context;
    struct canopus_ui_snapshot_v1 _snapshot;
    uint16_t _parent_stack[CANOPUS_UI_MAX_DEPTH];
    uint16_t _depth;
    uint8_t _active;
};

struct canopus_ui_context_v1 {
    const struct canopus_ui_backend_v1 *backend;
    void *backend_cookie;
    canopus_ui_event_handler_v1 event_handler;
    void *event_cookie;
    struct canopus_ui_snapshot_v1 committed;
    struct canopus_ui_tree_v1 staging;
    uint32_t dropped_events;
    uint8_t building;
    uint8_t has_committed;
};

struct canopus_ui_navigation_page_props_v1 {
    uint32_t struct_size;
    const char *title;
    uint32_t title_len;
};

struct canopus_ui_section_props_v1 {
    uint32_t struct_size;
    const char *title;
    uint32_t title_len;
};

struct canopus_ui_text_props_v1 {
    uint32_t struct_size;
    const char *text;
    uint32_t text_len;
    uint32_t style;
};

struct canopus_ui_status_row_props_v1 {
    uint32_t struct_size;
    const char *label;
    uint32_t label_len;
    const char *value;
    uint32_t value_len;
};

struct canopus_ui_button_props_v1 {
    uint32_t struct_size;
    const char *label;
    uint32_t label_len;
    uint32_t event_id;
    uint32_t enabled;
};

int32_t canopus_ui_context_init(
    struct canopus_ui_context_v1 *context,
    const struct canopus_ui_backend_v1 *backend,
    void *backend_cookie,
    canopus_ui_event_handler_v1 event_handler,
    void *event_cookie);

int32_t canopus_ui_tree_begin(
    struct canopus_ui_context_v1 *context,
    struct canopus_ui_tree_v1 **out_tree);

int32_t canopus_ui_navigation_page(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_navigation_page_props_v1 *props);
int32_t canopus_ui_section(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_section_props_v1 *props);
int32_t canopus_ui_text(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_text_props_v1 *props);
int32_t canopus_ui_status_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_status_row_props_v1 *props);
int32_t canopus_ui_button(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_button_props_v1 *props);

/* Ends the current navigation-page or section container. */
int32_t canopus_ui_end(struct canopus_ui_tree_v1 *tree);
int32_t canopus_ui_tree_commit(struct canopus_ui_tree_v1 *tree);
void canopus_ui_tree_abort(struct canopus_ui_tree_v1 *tree);

const struct canopus_ui_snapshot_v1 *canopus_ui_current(
    const struct canopus_ui_context_v1 *context);

int32_t canopus_ui_dispatch_event(
    struct canopus_ui_context_v1 *context, uint32_t generation,
    canopus_ui_node_id key, uint32_t event_id);

/* Thin convenience layer: every macro expands to the corresponding ordinary
 * builder call and does not retain hidden state or firmware pointers. */
#define CANOPUS_UI_BEGIN(context_, name_) \
    struct canopus_ui_tree_v1 *name_ = 0; \
    if (canopus_ui_tree_begin((context_), &(name_)) != CANOPUS_UI_OK) return CANOPUS_UI_ERR_STATE

#define CANOPUS_UI_COMMIT(tree_) canopus_ui_tree_commit((tree_))
#define CANOPUS_UI_END(tree_) canopus_ui_end((tree_))

/* Literal helpers keep ordinary application views compact while preserving the
 * explicit, versioned property structs underneath. String arguments must be
 * array-backed string literals, not char pointers. */
#define CANOPUS_UI_NAVIGATION_PAGE(tree_, key_, title_) \
    canopus_ui_navigation_page((tree_), (key_), \
        &(const struct canopus_ui_navigation_page_props_v1){ \
            sizeof(struct canopus_ui_navigation_page_props_v1), \
            (title_), (uint32_t)(sizeof(title_) - 1u) })
#define CANOPUS_UI_SECTION(tree_, key_, title_) \
    canopus_ui_section((tree_), (key_), \
        &(const struct canopus_ui_section_props_v1){ \
            sizeof(struct canopus_ui_section_props_v1), \
            (title_), (uint32_t)(sizeof(title_) - 1u) })
#define CANOPUS_UI_TEXT(tree_, key_, text_, style_) \
    canopus_ui_text((tree_), (key_), \
        &(const struct canopus_ui_text_props_v1){ \
            sizeof(struct canopus_ui_text_props_v1), \
            (text_), (uint32_t)(sizeof(text_) - 1u), (style_) })
#define CANOPUS_UI_STATUS_ROW(tree_, key_, label_, value_) \
    canopus_ui_status_row((tree_), (key_), \
        &(const struct canopus_ui_status_row_props_v1){ \
            sizeof(struct canopus_ui_status_row_props_v1), \
            (label_), (uint32_t)(sizeof(label_) - 1u), \
            (value_), (uint32_t)(sizeof(value_) - 1u) })
#define CANOPUS_UI_BUTTON(tree_, key_, label_, event_, enabled_) \
    canopus_ui_button((tree_), (key_), \
        &(const struct canopus_ui_button_props_v1){ \
            sizeof(struct canopus_ui_button_props_v1), \
            (label_), (uint32_t)(sizeof(label_) - 1u), \
            (event_), (enabled_) })

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_UI_H */
