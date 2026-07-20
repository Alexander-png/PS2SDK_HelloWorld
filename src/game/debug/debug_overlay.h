#ifndef DEBUG_OVERLAY_H
#define DEBUG_OVERLAY_H

#include "game_app.h"
#include "engine/text/text.h"
#include "game/text/text_block.h"
#include "engine/text/text_font_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef DEBUG_OVERLAY_TEXT_CAPACITY
#define DEBUG_OVERLAY_TEXT_CAPACITY 1024
#endif

typedef struct debug_overlay_desc {
    const char *font_desc_path;
    const char *font_atlas_path;

    short x;
    short y;
    short w;
    short h;

    unsigned short glyph_capacity;
    unsigned short line_capacity;

    text_color_t color;
    short tracking;
    int layer;
} debug_overlay_desc_t;

typedef struct debug_overlay {
    text_font_resource_t font_res;
    text_block_t block;

    text_rich_run_t *runs;
    text_layout_item_t *items;
    text_layout_line_t *lines;

    char text[DEBUG_OVERLAY_TEXT_CAPACITY];

    short x;
    short y;
    short w;
    short h;

    unsigned char ready;
    unsigned char dirty;
    unsigned char font_bound_logged;
} debug_overlay_t;

void debug_overlay_desc_init(debug_overlay_desc_t *desc);

int debug_overlay_init(game_app_t *app,
                       debug_overlay_t *ovl,
                       const debug_overlay_desc_t *desc);

void debug_overlay_shutdown(game_app_t *app,
                            debug_overlay_t *ovl);

void debug_overlay_update(game_app_t *app,
                          debug_overlay_t *ovl,
                          float dt);

int debug_overlay_set_text(debug_overlay_t *ovl,
                           const char *text);

int debug_overlay_printf(debug_overlay_t *ovl,
                         const char *fmt, ...);

void debug_overlay_set_box(debug_overlay_t *ovl,
                           short x, short y, short w, short h);

void debug_overlay_draw(const debug_overlay_t *ovl);

int debug_overlay_is_ready(const debug_overlay_t *ovl);

#ifdef __cplusplus
}
#endif

#endif /* DEBUG_OVERLAY_H */