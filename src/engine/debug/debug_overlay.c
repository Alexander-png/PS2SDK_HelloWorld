#include "engine/debug/debug_overlay.h"
#include "engine/logging/log.h"
#include "engine/memory/memory_arena.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef DEBUG_OVERLAY_DEFAULT_FONT_DESC
#define DEBUG_OVERLAY_DEFAULT_FONT_DESC "8bitoperator_32.fnt"
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_FONT_ATLAS
#define DEBUG_OVERLAY_DEFAULT_FONT_ATLAS "8bitoperator_32.png"
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_X
#define DEBUG_OVERLAY_DEFAULT_X 24
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_Y
#define DEBUG_OVERLAY_DEFAULT_Y 24
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_W
#define DEBUG_OVERLAY_DEFAULT_W 560
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_H
#define DEBUG_OVERLAY_DEFAULT_H 400
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_GLYPH_CAPACITY
#define DEBUG_OVERLAY_DEFAULT_GLYPH_CAPACITY 512
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_LINE_CAPACITY
#define DEBUG_OVERLAY_DEFAULT_LINE_CAPACITY 32
#endif

#ifndef DEBUG_OVERLAY_DEFAULT_LAYER
#define DEBUG_OVERLAY_DEFAULT_LAYER 100
#endif

static void debug_overlay_apply_style(debug_overlay_t *ovl,
                                      const debug_overlay_desc_t *desc)
{
    text_style_t style;

    if (!ovl || !desc)
        return;

    text_style_init(&style);
    style.color = desc->color;
    style.tracking = desc->tracking;
    style.layer = desc->layer;

    text_block_set_style(&ovl->block, &style);
}

static int debug_overlay_refresh(debug_overlay_t *ovl)
{
    if (!ovl)
        return -1;

    if (!ovl->block.font)
        return -1;

    text_block_set_box(&ovl->block, ovl->x, ovl->y, ovl->w, ovl->h);
    text_block_set_text(&ovl->block, ovl->text);

    if (text_block_refresh(&ovl->block) != 0) {
        LOGLN("[debug_overlay] refresh failed");
        return -1;
    }

    ovl->dirty = 0;
    return 0;
}

void debug_overlay_desc_init(debug_overlay_desc_t *desc)
{
    if (!desc)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->font_desc_path = DEBUG_OVERLAY_DEFAULT_FONT_DESC;
    desc->font_atlas_path = DEBUG_OVERLAY_DEFAULT_FONT_ATLAS;
    desc->x = DEBUG_OVERLAY_DEFAULT_X;
    desc->y = DEBUG_OVERLAY_DEFAULT_Y;
    desc->w = DEBUG_OVERLAY_DEFAULT_W;
    desc->h = DEBUG_OVERLAY_DEFAULT_H;
    desc->glyph_capacity = DEBUG_OVERLAY_DEFAULT_GLYPH_CAPACITY;
    desc->line_capacity = DEBUG_OVERLAY_DEFAULT_LINE_CAPACITY;
    desc->color = text_color_default();
    desc->tracking = 0;
    desc->layer = DEBUG_OVERLAY_DEFAULT_LAYER;
}

int debug_overlay_init(game_app_t *app,
                       debug_overlay_t *ovl,
                       const debug_overlay_desc_t *desc)
{
    text_font_resource_desc_t font_desc;

    if (!app || !ovl || !desc)
        return -1;

    memset(ovl, 0, sizeof(*ovl));

    ovl->x = desc->x;
    ovl->y = desc->y;
    ovl->w = desc->w;
    ovl->h = desc->h;
    ovl->text[0] = '\0';
    ovl->dirty = 1;
    ovl->ready = 0;
    ovl->font_bound_logged = 0;

    ovl->runs = (text_rich_run_t *)mem_arena_calloc(
        game_app_state_arena(app),
        desc->glyph_capacity,
        sizeof(text_rich_run_t),
        16
    );
    if (!ovl->runs) {
        LOGLN("[debug_overlay] runs buffer alloc failed");
        return -1;
    }

    ovl->items = (text_layout_item_t *)mem_arena_calloc(
        game_app_state_arena(app),
        desc->glyph_capacity,
        sizeof(text_layout_item_t),
        16
    );
    if (!ovl->items) {
        LOGLN("[debug_overlay] item buffer alloc failed");
        return -1;
    }

    ovl->lines = (text_layout_line_t *)mem_arena_calloc(
        game_app_state_arena(app),
        desc->line_capacity,
        sizeof(text_layout_line_t),
        16
    );
    if (!ovl->lines) {
        LOGLN("[debug_overlay] line buffer alloc failed");
        return -1;
    }

    text_block_init(&ovl->block,
                    ovl->runs,
                    desc->glyph_capacity,
                    ovl->items,
                    desc->glyph_capacity,
                    ovl->lines,
                    desc->line_capacity,
                    0.03f);

    text_block_set_align_h(&ovl->block, TEXT_ALIGN_LEFT);
    text_block_set_align_v(&ovl->block, TEXT_ALIGN_TOP);
    text_block_set_wrap_mode(&ovl->block, TEXT_WRAP_WORD);
    text_block_set_reveal_mode(&ovl->block, TEXT_REVEAL_NONE);

    debug_overlay_apply_style(ovl, desc);

    font_desc.fnt_path = desc->font_desc_path;
    font_desc.atlas_path = desc->font_atlas_path;

    if (text_font_resource_request(app, &ovl->font_res, &font_desc) != 0) {
        LOGLN("[debug_overlay] font request failed");
        return -1;
    }

    LOGLN("[debug_overlay] init requested descriptor=%s atlas=%s",
          font_desc.fnt_path,
          font_desc.atlas_path);

    return 0;
}

void debug_overlay_shutdown(game_app_t *app,
                            debug_overlay_t *ovl)
{
    if (!app || !ovl)
        return;

    text_font_resource_shutdown(app, &ovl->font_res);
    ovl->ready = 0;
    ovl->dirty = 0;

    LOGLN("[debug_overlay] shutdown");
}

void debug_overlay_update(game_app_t *app,
                          debug_overlay_t *ovl,
                          float dt)
{
    const text_font_t *font;

    (void)dt;

    if (!app || !ovl)
        return;

    text_font_resource_update(app, &ovl->font_res);

    if (!ovl->block.font && text_font_resource_is_ready(&ovl->font_res)) {
        font = text_font_resource_get_font(&ovl->font_res);
        text_block_set_font(&ovl->block, font);
        ovl->ready = 1;
        ovl->dirty = 1;

        if (!ovl->font_bound_logged && font) {
            LOGLN("[debug_overlay] font bound glyphs=%u kernings=%u",
                  (unsigned int)font->glyph_count,
                  (unsigned int)font->kerning_count);
            ovl->font_bound_logged = 1;
        }
    }

    if (ovl->ready && ovl->dirty) {
        if (debug_overlay_refresh(ovl) != 0)
            LOGLN("[debug_overlay] refresh failed during update");
    }
}

int debug_overlay_set_text(debug_overlay_t *ovl,
                           const char *text)
{
    size_t len;

    if (!ovl || !text)
        return -1;

    len = strlen(text);
    if (len >= sizeof(ovl->text))
        len = sizeof(ovl->text) - 1;

    memcpy(ovl->text, text, len);
    ovl->text[len] = '\0';
    ovl->dirty = 1;
    return 0;
}

int debug_overlay_printf(debug_overlay_t *ovl,
                         const char *fmt, ...)
{
    va_list ap;
    int n;

    if (!ovl || !fmt)
        return -1;

    va_start(ap, fmt);
    n = vsnprintf(ovl->text, sizeof(ovl->text), fmt, ap);
    va_end(ap);

    if (n < 0)
        return -1;

    ovl->text[sizeof(ovl->text) - 1] = '\0';
    ovl->dirty = 1;
    return 0;
}

void debug_overlay_set_box(debug_overlay_t *ovl,
                           short x, short y, short w, short h)
{
    if (!ovl)
        return;

    ovl->x = x;
    ovl->y = y;
    ovl->w = w;
    ovl->h = h;
    ovl->dirty = 1;
}

void debug_overlay_draw(const debug_overlay_t *ovl)
{
    if (!ovl || !ovl->ready)
        return;

    text_block_draw(&ovl->block);
}

int debug_overlay_is_ready(const debug_overlay_t *ovl)
{
    if (!ovl)
        return 0;

    return ovl->ready ? 1 : 0;
}