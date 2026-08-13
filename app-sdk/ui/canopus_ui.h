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
#define CANOPUS_UI_ABI_MINOR 4u

#define CANOPUS_UI_MAX_NODES        32u
#define CANOPUS_UI_MAX_DEPTH         8u
#define CANOPUS_UI_STRING_CAPACITY 1536u
#define CANOPUS_UI_ROUTER_MAX_DEPTH   8u
#define CANOPUS_UI_NO_NODE          0xFFFFu

#define CANOPUS_UI_OK             0
#define CANOPUS_UI_ERR_ARGUMENT  -1
#define CANOPUS_UI_ERR_STATE     -2
#define CANOPUS_UI_ERR_CAPACITY  -3
#define CANOPUS_UI_ERR_DUP_KEY   -4
#define CANOPUS_UI_ERR_BACKEND   -5
#define CANOPUS_UI_ERR_STALE     -6
#define CANOPUS_UI_ERR_DISABLED  -7
#define CANOPUS_UI_ERR_UNSUPPORTED -8

#define CANOPUS_UI_CAP_EXTENDED_COMPONENTS (1u << 0)
#define CANOPUS_UI_CAP_STYLE               (1u << 1)
#define CANOPUS_UI_CAP_LAYOUT              (1u << 2)
#define CANOPUS_UI_CAP_VALUES              (1u << 3)
#define CANOPUS_UI_CAP_NAVIGATION_HEADER   (1u << 4)
#define CANOPUS_UI_CAP_IMAGE               (1u << 5)
#define CANOPUS_UI_CAP_PROGRESS            (1u << 6)

typedef uint32_t canopus_ui_node_id;

enum canopus_ui_node_type {
    CANOPUS_UI_NODE_NAVIGATION_PAGE = 1,
    CANOPUS_UI_NODE_SECTION,
    CANOPUS_UI_NODE_TEXT,
    CANOPUS_UI_NODE_STATUS_ROW,
    CANOPUS_UI_NODE_BUTTON,
    CANOPUS_UI_NODE_ACTION_ROW,
    CANOPUS_UI_NODE_SWITCH_ROW,
    CANOPUS_UI_NODE_LIST,
    CANOPUS_UI_NODE_SCROLL,
    CANOPUS_UI_NODE_DIALOG,
    CANOPUS_UI_NODE_TOAST,
    CANOPUS_UI_NODE_IMAGE,
    CANOPUS_UI_NODE_ICON,
    CANOPUS_UI_NODE_RICH_TEXT,
    CANOPUS_UI_NODE_CHECKBOX,
    CANOPUS_UI_NODE_RADIO_ROW,
    CANOPUS_UI_NODE_SLIDER,
    CANOPUS_UI_NODE_PROGRESS,
    CANOPUS_UI_NODE_DIVIDER,
    CANOPUS_UI_NODE_SPACER,
    /* ABI 1.4 semantic extension; requires CAP_NAVIGATION_HEADER. */
    CANOPUS_UI_NODE_NAVIGATION_HEADER,
};

#define CANOPUS_UI_NODE_FLAG_ENABLED         (1u << 0)
#define CANOPUS_UI_NODE_FLAG_CHECKED         (1u << 1)
#define CANOPUS_UI_NODE_FLAG_SELECTED        (1u << 2)
#define CANOPUS_UI_NODE_FLAG_INDETERMINATE   (1u << 3)
#define CANOPUS_UI_NODE_FLAG_VISIBLE         (1u << 4)
#define CANOPUS_UI_NODE_FLAG_WRAP            (1u << 5)
#define CANOPUS_UI_NODE_FLAG_HEADER_BACK      (1u << 6)
#define CANOPUS_UI_NODE_FLAG_HEADER_CENTERED  (1u << 7)
#define CANOPUS_UI_NODE_FLAG_HEADER_ELEVATED  (1u << 8)

/* Public semantic customization. Values are target-independent: a backend maps
 * them to the closest approved stock token/prefab and must not expose private
 * firmware style IDs. Signed dimensions use -1 for automatic/inherit. */
enum canopus_ui_component_variant {
    CANOPUS_UI_VARIANT_DEFAULT = 0,
    CANOPUS_UI_VARIANT_PLAIN,
    CANOPUS_UI_VARIANT_FILLED,
    CANOPUS_UI_VARIANT_OUTLINED,
    CANOPUS_UI_VARIANT_TONAL,
    CANOPUS_UI_VARIANT_DESTRUCTIVE,
    CANOPUS_UI_VARIANT_COMPACT,
};

enum canopus_ui_color_role {
    CANOPUS_UI_COLOR_INHERIT = 0,
    CANOPUS_UI_COLOR_SURFACE,
    CANOPUS_UI_COLOR_SURFACE_ALT,
    CANOPUS_UI_COLOR_TEXT_PRIMARY,
    CANOPUS_UI_COLOR_TEXT_SECONDARY,
    CANOPUS_UI_COLOR_ACCENT,
    CANOPUS_UI_COLOR_SUCCESS,
    CANOPUS_UI_COLOR_WARNING,
    CANOPUS_UI_COLOR_DANGER,
    CANOPUS_UI_COLOR_DISABLED,
    CANOPUS_UI_COLOR_TRANSPARENT,
};

enum canopus_ui_axis {
    CANOPUS_UI_AXIS_VERTICAL = 0,
    CANOPUS_UI_AXIS_HORIZONTAL = 1,
};

enum canopus_ui_alignment {
    CANOPUS_UI_ALIGN_AUTO = 0,
    CANOPUS_UI_ALIGN_START,
    CANOPUS_UI_ALIGN_CENTER,
    CANOPUS_UI_ALIGN_END,
    CANOPUS_UI_ALIGN_STRETCH,
};

enum canopus_ui_justification {
    CANOPUS_UI_JUSTIFY_START = 0,
    CANOPUS_UI_JUSTIFY_CENTER,
    CANOPUS_UI_JUSTIFY_END,
    CANOPUS_UI_JUSTIFY_SPACE_BETWEEN,
    CANOPUS_UI_JUSTIFY_SPACE_AROUND,
    CANOPUS_UI_JUSTIFY_SPACE_EVENLY,
};

/* Semantic typography roles. Target backends must map supported roles to stock
 * typography instead of exposing private firmware style IDs. */
enum canopus_ui_text_style {
    CANOPUS_UI_TEXT_BODY = 0,
    CANOPUS_UI_TEXT_TITLE,
    CANOPUS_UI_TEXT_DESCRIPTION,
    CANOPUS_UI_TEXT_WARNING,
};

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

struct canopus_ui_style_v1 {
    uint16_t variant;
    uint16_t text_style;
    uint16_t foreground;
    uint16_t background;
    uint16_t accent;
    uint16_t border_color;
    int16_t corner_radius;
    int16_t border_width;
    uint16_t opacity; /* 0..1000; 0 means inherit. */
    uint16_t reserved;
};

struct canopus_ui_layout_v1 {
    int16_t width;
    int16_t height;
    int16_t min_width;
    int16_t min_height;
    int16_t max_width;
    int16_t max_height;
    int16_t margin_top;
    int16_t margin_right;
    int16_t margin_bottom;
    int16_t margin_left;
    int16_t padding_top;
    int16_t padding_right;
    int16_t padding_bottom;
    int16_t padding_left;
    int16_t gap;
    uint8_t axis;
    uint8_t align;
    uint8_t justify;
    uint8_t grow;
    uint8_t shrink;
    uint8_t reserved[3];
};

struct canopus_ui_value_v1 {
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    uint32_t resource_id;
};

#define CANOPUS_UI_STYLE_DEFAULT \
    { CANOPUS_UI_VARIANT_DEFAULT, CANOPUS_UI_TEXT_BODY, \
      CANOPUS_UI_COLOR_INHERIT, CANOPUS_UI_COLOR_INHERIT, \
      CANOPUS_UI_COLOR_INHERIT, CANOPUS_UI_COLOR_INHERIT, \
      -1, -1, 0u, 0u }
#define CANOPUS_UI_LAYOUT_DEFAULT \
    { -1, -1, -1, -1, -1, -1, 0, 0, 0, 0, 0, 0, 0, 0, 0, \
      CANOPUS_UI_AXIS_VERTICAL, CANOPUS_UI_ALIGN_AUTO, \
      CANOPUS_UI_JUSTIFY_START, 0u, 1u, { 0u, 0u, 0u } }

struct canopus_ui_snapshot_v1 {
    uint16_t abi_major;
    uint16_t abi_minor;
    uint32_t generation;
    uint16_t node_count;
    uint16_t string_used;
    struct canopus_ui_node_v1 nodes[CANOPUS_UI_MAX_NODES];
    char strings[CANOPUS_UI_STRING_CAPACITY];
    /* ABI 1.3 append-only metadata. The prefix through strings is identical to
     * ABI 1.2, so older backends can safely ignore this tail. */
    struct canopus_ui_style_v1 styles[CANOPUS_UI_MAX_NODES];
    struct canopus_ui_layout_v1 layouts[CANOPUS_UI_MAX_NODES];
    struct canopus_ui_value_v1 values[CANOPUS_UI_MAX_NODES];
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

/* Bounded semantic route stack. Route ids are application-owned values; this
 * structure never stores a firmware page/widget pointer. Generation-checked
 * back handling rejects events emitted for an earlier navigation state. */
struct canopus_ui_router_v1 {
    uint32_t routes[CANOPUS_UI_ROUTER_MAX_DEPTH];
    uint32_t generation;
    uint16_t depth;
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

struct canopus_ui_action_row_props_v1 {
    uint32_t struct_size;
    const char *label;
    uint32_t label_len;
    const char *detail;
    uint32_t detail_len;
    uint32_t event_id;
    uint32_t enabled;
};

struct canopus_ui_switch_row_props_v1 {
    uint32_t struct_size;
    const char *label;
    uint32_t label_len;
    const char *detail;
    uint32_t detail_len;
    uint32_t event_id;
    uint32_t checked;
    uint32_t enabled;
};

struct canopus_ui_navigation_header_props_v1 {
    uint32_t struct_size;
    const char *title;
    uint32_t title_len;
    const char *subtitle;
    uint32_t subtitle_len;
    uint32_t back_event_id;
    uint32_t show_back;
    uint32_t centered;
    uint32_t elevated;
};

/* Generic component record for the extended prefab catalog. `type` is supplied
 * separately so one stable property ABI covers containers, media, selection,
 * value controls and feedback components. */
struct canopus_ui_component_props_v1 {
    uint32_t struct_size;
    const char *primary;
    uint32_t primary_len;
    const char *secondary;
    uint32_t secondary_len;
    uint32_t event_id;
    uint32_t flags;
    int32_t value;
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    uint32_t resource_id;
};

int32_t canopus_ui_context_init(
    struct canopus_ui_context_v1 *context,
    const struct canopus_ui_backend_v1 *backend,
    void *backend_cookie,
    canopus_ui_event_handler_v1 event_handler,
    void *event_cookie);

/* Returns semantic features supported by the negotiated backend minor. A v1.2
 * backend can consume the stable snapshot prefix but cannot silently accept
 * v1.3 components or metadata that it would ignore; NavigationHeader is gated
 * separately at v1.4. */
uint32_t canopus_ui_context_capabilities(
    const struct canopus_ui_context_v1 *context);

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
int32_t canopus_ui_action_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_action_row_props_v1 *props);
int32_t canopus_ui_switch_row(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_switch_row_props_v1 *props);
int32_t canopus_ui_navigation_header(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_navigation_header_props_v1 *props);

/* Extended catalog entry point. Containers (list/scroll/dialog/header) are closed with
 * canopus_ui_end. Interactive types require a non-zero event id. */
int32_t canopus_ui_component(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key, uint16_t type,
    const struct canopus_ui_component_props_v1 *props);
int32_t canopus_ui_node_set_style(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_style_v1 *style);
int32_t canopus_ui_node_set_layout(
    struct canopus_ui_tree_v1 *tree, canopus_ui_node_id key,
    const struct canopus_ui_layout_v1 *layout);

/* Ends the current navigation-page or section container. */
int32_t canopus_ui_end(struct canopus_ui_tree_v1 *tree);
int32_t canopus_ui_tree_commit(struct canopus_ui_tree_v1 *tree);
void canopus_ui_tree_abort(struct canopus_ui_tree_v1 *tree);

const struct canopus_ui_snapshot_v1 *canopus_ui_current(
    const struct canopus_ui_context_v1 *context);

int32_t canopus_ui_dispatch_event(
    struct canopus_ui_context_v1 *context, uint32_t generation,
    canopus_ui_node_id key, uint32_t event_id);

int32_t canopus_ui_router_init(
    struct canopus_ui_router_v1 *router, uint32_t root_route);
int32_t canopus_ui_router_push(
    struct canopus_ui_router_v1 *router, uint32_t route);
int32_t canopus_ui_router_replace(
    struct canopus_ui_router_v1 *router, uint32_t route);
int32_t canopus_ui_router_pop(
    struct canopus_ui_router_v1 *router, uint32_t *out_route);
int32_t canopus_ui_router_back(
    struct canopus_ui_router_v1 *router, uint32_t generation,
    uint32_t *out_route);
int32_t canopus_ui_router_pop_to(
    struct canopus_ui_router_v1 *router, uint32_t route);
int32_t canopus_ui_router_clear_to_root(
    struct canopus_ui_router_v1 *router);
uint32_t canopus_ui_router_current(
    const struct canopus_ui_router_v1 *router);
uint16_t canopus_ui_router_depth(
    const struct canopus_ui_router_v1 *router);

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
#define CANOPUS_UI_ACTION_ROW(tree_, key_, label_, detail_, event_, enabled_) \
    canopus_ui_action_row((tree_), (key_), \
        &(const struct canopus_ui_action_row_props_v1){ \
            sizeof(struct canopus_ui_action_row_props_v1), \
            (label_), (uint32_t)(sizeof(label_) - 1u), \
            (detail_), (uint32_t)(sizeof(detail_) - 1u), \
            (event_), (enabled_) })
#define CANOPUS_UI_SWITCH_ROW(tree_, key_, label_, detail_, event_, checked_, enabled_) \
    canopus_ui_switch_row((tree_), (key_), \
        &(const struct canopus_ui_switch_row_props_v1){ \
            sizeof(struct canopus_ui_switch_row_props_v1), \
            (label_), (uint32_t)(sizeof(label_) - 1u), \
            (detail_), (uint32_t)(sizeof(detail_) - 1u), \
            (event_), (checked_), (enabled_) })
#define CANOPUS_UI_NAVIGATION_HEADER(tree_, key_, title_, subtitle_, back_event_, show_back_) \
    canopus_ui_navigation_header((tree_), (key_), \
        &(const struct canopus_ui_navigation_header_props_v1){ \
            sizeof(struct canopus_ui_navigation_header_props_v1), \
            (title_), (uint32_t)(sizeof(title_) - 1u), \
            (subtitle_), (uint32_t)(sizeof(subtitle_) - 1u), \
            (back_event_), (show_back_), 1u, 0u })

#define CANOPUS_UI_COMPONENT(tree_, key_, type_, primary_, secondary_, event_, flags_) \
    canopus_ui_component((tree_), (key_), (type_), \
        &(const struct canopus_ui_component_props_v1){ \
            sizeof(struct canopus_ui_component_props_v1), \
            (primary_), (uint32_t)(sizeof(primary_) - 1u), \
            (secondary_), (uint32_t)(sizeof(secondary_) - 1u), \
            (event_), (flags_), 0, 0, 0, 0, 0u })
#define CANOPUS_UI_LIST(tree_, key_, title_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_LIST, (title_), "", 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_SCROLL(tree_, key_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_SCROLL, "", "", 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_DIALOG(tree_, key_, title_, message_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_DIALOG, (title_), (message_), 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_TOAST(tree_, key_, message_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_TOAST, (message_), "", 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_CHECKBOX(tree_, key_, label_, event_, checked_, enabled_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_CHECKBOX, (label_), "", (event_), \
        ((enabled_) ? CANOPUS_UI_NODE_FLAG_ENABLED : 0u) | \
        ((checked_) ? CANOPUS_UI_NODE_FLAG_CHECKED : 0u) | CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_RADIO_ROW(tree_, key_, label_, detail_, event_, selected_, enabled_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_RADIO_ROW, (label_), (detail_), \
        (event_), ((enabled_) ? CANOPUS_UI_NODE_FLAG_ENABLED : 0u) | \
        ((selected_) ? CANOPUS_UI_NODE_FLAG_SELECTED : 0u) | CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_IMAGE(tree_, key_, description_, resource_) \
    canopus_ui_component((tree_), (key_), CANOPUS_UI_NODE_IMAGE, \
        &(const struct canopus_ui_component_props_v1){ \
            sizeof(struct canopus_ui_component_props_v1), \
            (description_), (uint32_t)(sizeof(description_) - 1u), \
            "", 0u, 0u, CANOPUS_UI_NODE_FLAG_VISIBLE, \
            0, 0, 0, 0, (resource_) })
#define CANOPUS_UI_PROGRESS(tree_, key_, value_, minimum_, maximum_) \
    canopus_ui_component((tree_), (key_), CANOPUS_UI_NODE_PROGRESS, \
        &(const struct canopus_ui_component_props_v1){ \
            sizeof(struct canopus_ui_component_props_v1), \
            "", 0u, "", 0u, 0u, CANOPUS_UI_NODE_FLAG_VISIBLE, \
            (value_), (minimum_), (maximum_), 0, 0u })
#define CANOPUS_UI_DIVIDER(tree_, key_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_DIVIDER, "", "", 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)
#define CANOPUS_UI_SPACER(tree_, key_) \
    CANOPUS_UI_COMPONENT((tree_), (key_), CANOPUS_UI_NODE_SPACER, "", "", 0u, \
                         CANOPUS_UI_NODE_FLAG_VISIBLE)

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_UI_H */
