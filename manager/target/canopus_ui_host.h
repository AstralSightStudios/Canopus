/*
 * canopus_ui_host.h — generic native UI backend for Band 10 Pro and Band 11.
 *
 * Renders a canopus_ui snapshot into the widgets of one firmware page (the stock
 * LVX title, list rows, labels, images and bars) and routes row activations back
 * as semantic events. It knows nothing about the Manager: the Manager's pages
 * and the pages that modules open (ADR-CAN-010) are all views of this backend.
 * Firmware widget pointers stay inside the view.
 *
 * Supported snapshot content: navigation page/header (the page title), sections,
 * lists, scrolls and dividers/spacers (flattened into the page's scrolling
 * body), text and rich text, status/action/switch/checkbox/radio rows, buttons,
 * images, progress bars, and dialogs. A dialog with a message and two buttons is a
 * stock message box where the target has one recovered; otherwise (and for any
 * other dialog shape) its children are shown inline. Anything else makes apply fail and leaves the
 * previous content intact, as the backend contract requires.
 */
#ifndef CANOPUS_UI_HOST_H
#define CANOPUS_UI_HOST_H

#include <stdint.h>

#include "canopus_ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANOPUS_UI_HOST_MAX_ROWS     64u
#define CANOPUS_UI_HOST_MAX_LABELS   64u
#define CANOPUS_UI_HOST_MAX_IMAGES   16u
#define CANOPUS_UI_HOST_MAX_PROGRESS 32u
#define CANOPUS_UI_HOST_MAX_VIEWS    16u
#define CANOPUS_UI_HOST_MAX_PLACED  256u

/* View flags. */
#define CANOPUS_UI_HOST_ROOT_PAGE (1u << 0) /* no back affordance in the title */

struct canopus_ui_host_view;

/* The semantic event a row, button or dialog button delivers. */
struct canopus_ui_host_binding {
    uint32_t generation;
    canopus_ui_node_id key;
    uint32_t event_id;
};

struct canopus_ui_host_hooks {
    /* The page title's back affordance was used (non-root pages). Optional; the
     * stock title handles back itself when it is NULL. */
    void (*back)(struct canopus_ui_host_view *view);
    /* Around the delivery of a row's semantic event; `result` is the
     * canopus_ui_dispatch_event result. Optional. */
    void (*before_event)(struct canopus_ui_host_view *view, uint32_t row,
                         const struct canopus_ui_host_binding *binding);
    void (*after_event)(struct canopus_ui_host_view *view, uint32_t row,
                        const struct canopus_ui_host_binding *binding,
                        int32_t result);
    /* A snapshot was applied; `first` is the first widget placed. Optional. */
    void (*rendered)(struct canopus_ui_host_view *view, void *first);
};

/* A message box button record as the firmware reads it. Band 11's is wider; the
 * fields past `visual` stay zero. */
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
    char button_text[2][32]; /* the message box keeps these pointers */
    uint8_t active;
    uint8_t dispatching;
};

struct canopus_ui_host_view {
    /* Set by canopus_ui_host_attach; everything below is private. */
    void *root;
    void *firmware_page;
    struct canopus_ui_context_v1 *ui;
    const struct canopus_ui_host_hooks *hooks;
    uint32_t flags;
    uint8_t active;       /* attached to a live firmware page */
    uint8_t interactive;  /* the page is on top: row events are delivered */

    void *page_title;
    void *content_root;
    void *rows[CANOPUS_UI_HOST_MAX_ROWS];
#ifdef CANOPUS_MGR_BAND11
    void *row_event_objects[CANOPUS_UI_HOST_MAX_ROWS];
#endif
    void *labels[CANOPUS_UI_HOST_MAX_LABELS];
    void *images[CANOPUS_UI_HOST_MAX_IMAGES];
    void *progress[CANOPUS_UI_HOST_MAX_PROGRESS];
    int32_t progress_minimum[CANOPUS_UI_HOST_MAX_PROGRESS];
    int32_t progress_maximum[CANOPUS_UI_HOST_MAX_PROGRESS];
    int32_t progress_value[CANOPUS_UI_HOST_MAX_PROGRESS];
    uint8_t row_kinds[CANOPUS_UI_HOST_MAX_ROWS];
    canopus_ui_node_id row_keys[CANOPUS_UI_HOST_MAX_ROWS];
    struct canopus_ui_host_binding bindings[CANOPUS_UI_HOST_MAX_ROWS];
    struct canopus_ui_host_dialog dialog;
    uint32_t rendered_generation;
    uint16_t placed[CANOPUS_UI_HOST_MAX_PLACED];       /* the last render's placement chain */
    uint32_t placed_sig[CANOPUS_UI_HOST_MAX_PLACED];
    uint8_t placed_count;
    uint8_t scroll_locked; /* the content fits: its scrolling is off */
};

/* The backend to pass to canopus_ui_context_init with the view as its cookie. */
extern const struct canopus_ui_backend_v1 canopus_ui_host_backend;

/* Binds the view to a firmware page that is being created: `root` is the page
 * root object the create callback received, `page` the firmware page descriptor.
 * Resets all state. The view is deliverable once `ui` is set and `interactive`
 * is raised by the page lifecycle. */
void canopus_ui_host_attach(struct canopus_ui_host_view *view, void *root,
                            void *firmware_page,
                            const struct canopus_ui_host_hooks *hooks,
                            uint32_t flags);
void canopus_ui_host_bind(struct canopus_ui_host_view *view,
                          struct canopus_ui_context_v1 *ui);
void canopus_ui_host_set_interactive(struct canopus_ui_host_view *view,
                                     int interactive);
/* The firmware page is being destroyed: closes a dialog and forgets every widget. */
void canopus_ui_host_detach(struct canopus_ui_host_view *view);

#ifdef __cplusplus
}
#endif

#endif /* CANOPUS_UI_HOST_H */
