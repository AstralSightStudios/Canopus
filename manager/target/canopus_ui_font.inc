/* Native font references belong to native labels, independently of view memory.
 * Included after the object/event primitives; only exact reviewed targets bind
 * these privileged entries. No font object crosses the public module ABI. */
#if defined(FW_UI_FONT_CREATE) && defined(FW_UI_FONT_RELEASE) && \
    defined(FW_UI_FONT_MANAGER_SLOT) && defined(FW_UI_FONT_WAIT) && \
    defined(FW_UI_FONT_DRAW_UNITS) && defined(FW_UI_FONT_DISPLAY_LIST) && defined(FW_UI_FONT_VG_DISPATCH) && \
    defined(FW_UI_FONT_SW_DISPATCH) && defined(FW_UI_FONT_EVENT_DELETE)
#define HOST_FONT 1
#define HOST_FONT_CAPS CANOPUS_UI_CAP_FONT
#ifndef FW_UI_FONT_DISPLAY_NEXT_OFFSET
#define FW_UI_FONT_DISPLAY_NEXT_OFFSET 796u
#define FW_UI_FONT_DISPLAY_LAYER_OFFSET 680u
#define FW_UI_FONT_UNIT_DISPATCH_OFFSET 16u
#define FW_UI_FONT_UNIT_ACTIVE_OFFSET 32u
#endif
#define HOST_FONT_CACHE_LIMIT (CANOPUS_UI_HOST_MAX_VIEWS * CANOPUS_UI_MAX_NODES)
struct host_font {
    struct host_font *next;
    void *native, *manager;
    uint32_t references;
    uint16_t pixels;
};
struct host_font_label {
    struct host_font_label *next;
    void *object;
    struct host_font *font;
};
static struct host_font *s_fonts;
static struct host_font_label *s_font_labels;
static uint32_t s_font_count;

static void *host_font_alloc(uint32_t size)
{
#if defined(CANOPUS_MGR_REDMI_WATCH)
    return canopus_fw_mm_memalign_default(4u, size);
#else
    return ((void *(*)(uint32_t))(uintptr_t)CANOPUS_SUP_HEAP_MALLOC)(size);
#endif
}
static void host_font_free(void *pointer)
{
#if defined(CANOPUS_MGR_REDMI_WATCH)
    canopus_fw_mm_free_default(pointer);
#else
    ((void (*)(void *))(uintptr_t)CANOPUS_SUP_HEAP_FREE)(pointer);
#endif
}

/* No arbitrary heartbeat delay is a font fence. The native wait drains its
 * borrowed CPU glyph references; it makes no successful-GPU-rendering claim. */
static int host_font_drain(void)
{
    void *unit, *display;
    /* A render callback can have queued tasks before any unit is active. */
    for (display = *(void **)((uint8_t *)(uintptr_t)FW_UI_FONT_DISPLAY_LIST + 4u);
         display != NULL; display = *(void **)((uint8_t *)display + FW_UI_FONT_DISPLAY_NEXT_OFFSET)) {
        void *layer;
        if (((uint8_t *)display)[58] & 2u) return 0;
        for (layer = *(void **)((uint8_t *)display + FW_UI_FONT_DISPLAY_LAYER_OFFSET); layer != NULL;
             layer = *(void **)((uint8_t *)layer + 108u))
            if (*(void **)((uint8_t *)layer + 100u) != NULL) return 0;
    }
    for (unit = *(void **)(uintptr_t)FW_UI_FONT_DRAW_UNITS; unit != NULL; unit = *(void **)unit) {
        uintptr_t dispatch = *(uintptr_t *)((uint8_t *)unit + FW_UI_FONT_UNIT_DISPATCH_OFFSET);
        if (*(void **)((uint8_t *)unit + FW_UI_FONT_UNIT_ACTIVE_OFFSET) != NULL ||
            (dispatch != FW_UI_FONT_VG_DISPATCH && dispatch != FW_UI_FONT_SW_DISPATCH)) return 0;
    }
    for (unit = *(void **)(uintptr_t)FW_UI_FONT_DRAW_UNITS; unit != NULL; unit = *(void **)unit)
        if (*(uintptr_t *)((uint8_t *)unit + FW_UI_FONT_UNIT_DISPATCH_OFFSET) == FW_UI_FONT_VG_DISPATCH)
            ((void (*)(void *))(uintptr_t)FW_UI_FONT_WAIT)(unit);
    return 1;
}

/* Native glyph descriptors borrow the wrapper. Zero label references alone
 * never authorize release: retire only after the exact native drain boundary. */
static void host_font_collect(void)
{
    struct host_font **link = &s_fonts;
    int pending = 0;
    while (*link != NULL) {
        if ((*link)->references == 0u) pending = 1;
        link = &(*link)->next;
    }
    if (!pending) return;
    if (!host_font_drain()) return;
    link = &s_fonts;
    while (*link != NULL) {
        struct host_font *font = *link;
        if (font->references != 0u) { link = &font->next; continue; }
        *link = font->next;
        ((void (*)(void *, void *))(uintptr_t)FW_UI_FONT_RELEASE)(font->manager,font->native);
        host_font_free(font);
        s_font_count--;
    }
}
static void host_font_put(struct host_font *font)
{ if (font != NULL) font->references--; }
static struct host_font *host_font_get(uint16_t pixels)
{
    struct host_font *font;
    void *root = *(void **)(uintptr_t)FW_UI_FONT_MANAGER_SLOT;
    void *manager;
    struct { const char *family; uint16_t pixels, style; } key = {"MiSans-Medium",pixels,0};
    if (root == NULL) return NULL;
    manager = *(void **)((uint8_t *)root + 28u);
    if (manager == NULL) return NULL;
    for (font = s_fonts; font != NULL; font = font->next)
        if (font->pixels == pixels && font->manager == manager) { font->references++; return font; }
    host_font_collect();
    if (s_font_count == HOST_FONT_CACHE_LIMIT) return NULL;
    font = host_font_alloc(sizeof(*font));
    if (font == NULL) return NULL;
    font->native = ((void *(*)(void *, const void *))(uintptr_t)FW_UI_FONT_CREATE)(manager,&key);
    if (font->native == NULL) { host_font_free(font); return NULL; }
    font->manager = manager; font->pixels = pixels; font->references = 1;
    font->next = s_fonts; s_fonts = font; s_font_count++;
    return font;
}
static struct host_font_label *host_font_label(void *object)
{
    struct host_font_label *label;
    for (label = s_font_labels; label != NULL; label = label->next)
        if (label->object == object) return label;
    return NULL;
}
static int host_font_owned(void *object)
{
    struct host_font_label *label = host_font_label(object);
    return label != NULL && label->font != NULL;
}
static void host_font_deleted(void *event)
{
#ifdef FW_UI_FONT_EVENT_USER_DATA
    struct host_font_label *label = FW_UI_FONT_EVENT_USER_DATA(event);
#else
    struct host_font_label *label = *(struct host_font_label **)((uint8_t *)event + 12u);
#endif
    struct host_font_label **link = &s_font_labels;
    while (*link != label) link = &(*link)->next;
    *link = label->next;
    host_font_put(label->font);
    host_font_free(label);
    /* Native DELETE is sent before the full object subtree is reclaimed.
     * Collection happens after apply/delete returns or on the owner heartbeat. */
}
static int host_font_track(void *object)
{
    struct host_font_label *label = host_font_alloc(sizeof(*label));
    void *registration;
    if (label == NULL) return 0;
    label->object = object; label->font = NULL;
#ifndef CANOPUS_MGR_BAND11
    registration = ((void *(*)(void *, void (*)(void *), uint32_t, void *))(uintptr_t)FW_LVX_EVENT_ADD)(
#else
    registration = ((void *(*)(void *, void (*)(void *), uint32_t, void *))(uintptr_t)FW_LV_EVENT_ADD)(
#endif
        object,host_font_deleted,FW_UI_FONT_EVENT_DELETE,label);
    if (registration == NULL) { host_font_free(label); return 0; }
    label->next = s_font_labels; s_font_labels = label;
    return 1;
}
static void host_font_set(void *object, struct host_font *font)
{
    struct host_font_label *label = host_font_label(object);
    if (label->font != font) {
        if (font != NULL) font->references++;
        host_font_put(label->font);
        label->font = font;
    }
    if (font != NULL)
        ((void (*)(void *, uint32_t, uintptr_t, uint32_t))(uintptr_t)FW_UI_STYLE_SET)(
            object,90u,(uintptr_t)font->native,0u);
}
#else
#define HOST_FONT_CAPS 0u
#endif
