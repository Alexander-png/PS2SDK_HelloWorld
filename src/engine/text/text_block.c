#include "engine/text/text_block.h"
#include "engine/logging/log.h"

#include <string.h>

static void text_block_mark_dirty(text_block_t *tb, unsigned int flags)
{
    if (!tb)
        return;

    tb->dirty_flags |= flags;
}

static void text_block_apply_horizontal_alignment(text_block_t *tb)
{
    unsigned short line_index;

    if (!tb)
        return;

    if (tb->align_h == TEXT_ALIGN_LEFT)
        return;

    if (tb->box.w <= 0)
        return;

    if (!tb->use_rich_text) {
        for (line_index = 0; line_index < tb->layout.line_count; ++line_index) {
            text_layout_line_t *line = &tb->layout.lines[line_index];
            short offset_x = 0;
            unsigned short i;
            unsigned short first;
            unsigned short end;

            if (line->glyph_count == 0)
                continue;

            if (tb->align_h == TEXT_ALIGN_CENTER)
                offset_x = (short)((tb->box.w - line->width) / 2);
            else if (tb->align_h == TEXT_ALIGN_RIGHT)
                offset_x = (short)(tb->box.w - line->width);

            if (offset_x == 0)
                continue;

            first = line->first_glyph;
            end = (unsigned short)(first + line->glyph_count);

            for (i = first; i < end; ++i)
                tb->layout.glyphs[i].x = (short)(tb->layout.glyphs[i].x + offset_x);
        }
    } else {
        for (line_index = 0; line_index < tb->rich_layout.line_count; ++line_index) {
            text_layout_line_t *line = &tb->rich_layout.lines[line_index];
            short offset_x = 0;
            unsigned short i;
            unsigned short first;
            unsigned short end;

            if (line->glyph_count == 0)
                continue;

            if (tb->align_h == TEXT_ALIGN_CENTER)
                offset_x = (short)((tb->box.w - line->width) / 2);
            else if (tb->align_h == TEXT_ALIGN_RIGHT)
                offset_x = (short)(tb->box.w - line->width);

            if (offset_x == 0)
                continue;

            first = line->first_glyph;
            end = (unsigned short)(first + line->glyph_count);

            for (i = first; i < end; ++i)
                tb->rich_layout.items[i].x = (short)(tb->rich_layout.items[i].x + offset_x);
        }
    }
}

static void text_block_apply_vertical_alignment(text_block_t *tb)
{
    unsigned short i;
    short offset_y = 0;
    short layout_height;

    if (!tb)
        return;

    if (tb->align_v == TEXT_ALIGN_TOP)
        return;

    if (tb->box.h <= 0)
        return;

    layout_height = tb->use_rich_text ? tb->rich_layout.height : tb->layout.height;
    if (layout_height >= tb->box.h)
        return;

    if (tb->align_v == TEXT_ALIGN_MIDDLE)
        offset_y = (short)((tb->box.h - layout_height) / 2);
    else if (tb->align_v == TEXT_ALIGN_BOTTOM)
        offset_y = (short)(tb->box.h - layout_height);

    if (offset_y == 0)
        return;

    if (!tb->use_rich_text) {
        for (i = 0; i < tb->layout.glyph_count; ++i)
            tb->layout.glyphs[i].y = (short)(tb->layout.glyphs[i].y + offset_y);
    } else {
        for (i = 0; i < tb->rich_layout.item_count; ++i)
            tb->rich_layout.items[i].y = (short)(tb->rich_layout.items[i].y + offset_y);
    }
}

void text_block_init(text_block_t *tb,
                     text_layout_glyph_t *glyph_buffer,
                     unsigned short glyph_capacity,
                     text_layout_item_t *rich_item_buffer,
                     unsigned short rich_item_capacity,
                     text_layout_line_t *line_buffer,
                     unsigned short line_capacity,
                     float seconds_per_glyph)
{
    if (!tb)
        return;

    memset(tb, 0, sizeof(*tb));

    text_layout_init(&tb->layout,
                     glyph_buffer,
                     glyph_capacity,
                     line_buffer,
                     line_capacity);

    text_rich_layout_init(&tb->rich_layout,
                          rich_item_buffer,
                          rich_item_capacity,
                          line_buffer,
                          line_capacity);

    text_style_init(&tb->params.style);
    tb->params.origin_x = 0;
    tb->params.origin_y = 0;
    tb->params.max_width = 0;
    tb->params.wrap_mode = TEXT_WRAP_NONE;

    text_reveal_state_init(&tb->reveal, seconds_per_glyph);
    tb->reveal_mode = TEXT_REVEAL_GLYPH;

    tb->font = NULL;
    tb->text_utf8 = NULL;
    tb->rich_text_utf8 = NULL;
    tb->use_rich_text = 0;

    tb->box.x = 0;
    tb->box.y = 0;
    tb->box.w = 0;
    tb->box.h = 0;

    tb->align_h = TEXT_ALIGN_LEFT;
    tb->align_v = TEXT_ALIGN_TOP;
    tb->dirty_flags = TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN;

    tb->effect_time_seconds = 0.0f;
}

void text_block_set_font(text_block_t *tb, const text_font_t *font)
{
    if (!tb)
        return;

    tb->font = font;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_text(text_block_t *tb, const char *utf8_text)
{
    if (!tb)
        return;

    tb->text_utf8 = utf8_text;
    tb->use_rich_text = 0;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_rich_text(text_block_t *tb, const char *markup_utf8)
{
    if (!tb)
        return;

    tb->rich_text_utf8 = markup_utf8;
    tb->use_rich_text = 1;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_reveal_mode(text_block_t *tb, text_reveal_mode_t mode)
{
    if (!tb)
        return;

    if (tb->reveal_mode == mode)
        return;

    tb->reveal_mode = mode;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT);
}

void text_block_set_origin(text_block_t *tb, short x, short y)
{
    if (!tb)
        return;

    tb->box.x = x;
    tb->box.y = y;
    tb->params.origin_x = x;
    tb->params.origin_y = y;

    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_box(text_block_t *tb, short x, short y, short w, short h)
{
    if (!tb)
        return;

    tb->box.x = x;
    tb->box.y = y;
    tb->box.w = w;
    tb->box.h = h;

    tb->params.origin_x = x;
    tb->params.origin_y = y;
    tb->params.max_width = w;

    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_align_h(text_block_t *tb, text_align_h_t align_h)
{
    if (!tb)
        return;

    if (tb->align_h == align_h)
        return;

    tb->align_h = align_h;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_align_v(text_block_t *tb, text_align_v_t align_v)
{
    if (!tb)
        return;

    if (tb->align_v == align_v)
        return;

    tb->align_v = align_v;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_wrap_mode(text_block_t *tb, text_wrap_mode_t wrap_mode)
{
    if (!tb)
        return;

    if (tb->params.wrap_mode == wrap_mode)
        return;

    tb->params.wrap_mode = wrap_mode;
    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN);
}

void text_block_set_style(text_block_t *tb, const text_style_t *style)
{
    unsigned short i;

    if (!tb || !style)
        return;

    tb->params.style = *style;

    if (!tb->use_rich_text) {
        for (i = 0; i < tb->layout.glyph_count; ++i) {
            tb->layout.glyphs[i].color = style->color;
            tb->layout.glyphs[i].layer = style->layer;
        }
    } else {
        for (i = 0; i < tb->rich_layout.item_count; ++i) {
            tb->rich_layout.items[i].layer = style->layer;
        }
    }

    text_block_mark_dirty(tb, TEXT_BLOCK_DIRTY_STYLE);
}

int text_block_refresh(text_block_t *tb)
{
    if (!tb)
        return -1;

    if (!tb->dirty_flags)
        return 0;

    if ((tb->dirty_flags & (TEXT_BLOCK_DIRTY_LAYOUT | TEXT_BLOCK_DIRTY_ALIGN)) != 0) {
        if (!tb->font)
            return -1;

        if (!tb->font->glyphs || tb->font->glyph_count == 0 || tb->font->tex_id < 0)
            return -1;

        LOGLN("[text_block] refresh rich=%d wrap_mode=%d reveal_mode=%d max_width=%d dirty=0x%X",
              tb->use_rich_text,
              (int)tb->params.wrap_mode,
              (int)tb->reveal_mode,
              (int)tb->params.max_width,
              (unsigned int)tb->dirty_flags);

        if (!tb->use_rich_text) {
            if (!tb->text_utf8)
                return -1;

            text_layout_reset(&tb->layout);

            switch (tb->params.wrap_mode) {
                case TEXT_WRAP_NONE:
                    if (text_layout_build_plain(&tb->layout,
                                                tb->font,
                                                tb->text_utf8,
                                                &tb->params) != 0) {
                        LOGLN("[text_block] text_layout_build_plain failed");
                        return -1;
                    }
                    break;

                case TEXT_WRAP_WORD:
                case TEXT_WRAP_CHAR:
                    if (tb->params.max_width <= 0) {
                        if (text_layout_build_plain(&tb->layout,
                                                    tb->font,
                                                    tb->text_utf8,
                                                    &tb->params) != 0) {
                            LOGLN("[text_block] text_layout_build_plain failed");
                            return -1;
                        }
                    } else {
                        if (text_layout_build_boxed(&tb->layout,
                                                    tb->font,
                                                    tb->text_utf8,
                                                    &tb->params) != 0) {
                            LOGLN("[text_block] text_layout_build_boxed failed wrap_mode=%d",
                                  (int)tb->params.wrap_mode);
                            return -1;
                        }
                    }
                    break;

                default:
                    LOGLN("[text_block] unknown wrap_mode=%d", (int)tb->params.wrap_mode);
                    return -1;
            }
        } else {
            text_rich_style_t base_style;
            text_rich_run_t runs_local[64];
            unsigned short run_count = 0;

            if (!tb->rich_text_utf8)
                return -1;

            text_rich_layout_reset(&tb->rich_layout);

            text_rich_style_init(&base_style);
            base_style.color = tb->params.style.color;

            if (text_rich_parse(tb->rich_text_utf8,
                                &base_style,
                                runs_local,
                                64,
                                &run_count) != 0) {
                LOGLN("[text_block] text_rich_parse failed");
                return -1;
            }

            if (text_rich_layout_build_plain(&tb->rich_layout,
                                             tb->font,
                                             runs_local,
                                             run_count,
                                             &tb->params,
                                             tb->reveal_mode) != 0) {
                LOGLN("[text_block] text_rich_layout_build_plain failed");
                return -1;
            }
        }

        text_block_apply_horizontal_alignment(tb);
        text_block_apply_vertical_alignment(tb);
    }

    tb->dirty_flags &= ~(TEXT_BLOCK_DIRTY_LAYOUT |
                         TEXT_BLOCK_DIRTY_ALIGN |
                         TEXT_BLOCK_DIRTY_STYLE);

    return 0;
}

short text_block_width(const text_block_t *tb)
{
    if (!tb)
        return 0;

    return tb->use_rich_text ? tb->rich_layout.width : tb->layout.width;
}

short text_block_height(const text_block_t *tb)
{
    if (!tb)
        return 0;

    return tb->use_rich_text ? tb->rich_layout.height : tb->layout.height;
}

void text_block_reveal_reset(text_block_t *tb)
{
    if (!tb)
        return;

    text_reveal_state_reset(&tb->reveal);
}

void text_block_reveal_finish(text_block_t *tb)
{
    unsigned short total_units;

    if (!tb)
        return;

    total_units = tb->use_rich_text
        ? tb->rich_layout.reveal_group_count
        : tb->layout.glyph_count;

    text_reveal_state_finish(&tb->reveal, total_units);
}

void text_block_update(text_block_t *tb, float dt)
{
    unsigned short total_units;

    if (!tb)
        return;

    total_units = tb->use_rich_text
        ? tb->rich_layout.reveal_group_count
        : tb->layout.glyph_count;

    tb->effect_time_seconds += dt;

    text_reveal_state_update(&tb->reveal, total_units, dt);
}

void text_block_draw(const text_block_t *tb)
{
    if (!tb || !tb->font)
        return;

    if (!tb->use_rich_text) {
        text_draw_layout(tb->font, &tb->layout, &tb->reveal);
    } else {
        text_rich_draw_layout(tb->font,
                      &tb->rich_layout,
                      &tb->reveal,
                      tb->effect_time_seconds);
    }
}