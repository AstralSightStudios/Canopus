/* Generic bounded native UI backend. Firmware objects never cross the ABI. */
#ifndef CANOPUS_UI_HOST_H
#define CANOPUS_UI_HOST_H
#include <stdint.h>
#include "canopus_ui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define CANOPUS_UI_HOST_MAX_VIEWS 16u
#define CANOPUS_UI_HOST_MAX_ROWS CANOPUS_UI_MAX_NODES
#define CANOPUS_UI_HOST_ROOT_PAGE (1u << 0)
struct canopus_ui_host_view;
struct canopus_ui_host_binding {
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
};
struct canopus_ui_host_hooks {
    void (*back)(struct canopus_ui_host_view *view);
    void (*before_event)(struct canopus_ui_host_view *view, uint32_t slot,
                         const struct canopus_ui_host_binding *binding);
    void (*after_event)(struct canopus_ui_host_view *view, uint32_t slot,
                        const struct canopus_ui_host_binding *binding, int32_t result);
    void (*rendered)(struct canopus_ui_host_view *view, void *first);
    uint32_t (*time_ms)(struct canopus_ui_host_view *view);
};
struct canopus_ui_host_msgbox_button {
    void (*callback)(void *event);
    const char *text;
    void *user_data;
    const void *visual;
    uint32_t reserved;
#ifdef CANOPUS_MGR_BAND11
    uint32_t reserved2[2];
#endif
};
struct canopus_ui_host_dialog {
    void *native_box;
    struct canopus_ui_host_msgbox_button buttons[2];
    struct canopus_ui_host_binding bindings[2];
    char button_text[2][32];
    uint8_t active, dispatching;
};
struct canopus_ui_host_node {
    void *object;
    void *event_object;
    void *primary;
    void *secondary;
    void *indicator;
    void *extent_object;
    struct canopus_ui_host_binding binding;
    canopus_ui_node_id key, parent_key;
    int32_t x, y, width, height, extent_height;
    uint16_t type;
    uint8_t kind, visible, dirty, scroll_locked;
};
struct canopus_ui_host_view {
    void *root;
    void *firmware_page;
    struct canopus_ui_context_v1 *ui;
    const struct canopus_ui_host_hooks *hooks;
    uint32_t flags;
    uint8_t active, interactive;
    void *page_title;
    void *content_root;
    void *toast_object;
    uint32_t toast_key, toast_deadline, toast_generation, toast_text_hash;
    uint32_t feedback_clock, title_token;
    struct canopus_ui_host_binding title_binding;
    uint8_t toast_visible, title_back;
    struct canopus_ui_host_node nodes[CANOPUS_UI_MAX_NODES];
    struct canopus_ui_host_dialog dialog;
    uint32_t rendered_generation;
    int32_t content_height;
    uint8_t scroll_locked, render_valid;
};
extern const struct canopus_ui_backend_v1 canopus_ui_host_backend;
void canopus_ui_host_attach(struct canopus_ui_host_view *, void *, void *,
                            const struct canopus_ui_host_hooks *, uint32_t);
void canopus_ui_host_bind(struct canopus_ui_host_view *, struct canopus_ui_context_v1 *);
void canopus_ui_host_set_interactive(struct canopus_ui_host_view *, int);
void canopus_ui_host_detach(struct canopus_ui_host_view *);
void canopus_ui_host_poll(uint32_t now);
#ifdef __cplusplus
}
#endif
#endif
