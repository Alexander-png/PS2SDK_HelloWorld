#include "engine/text/text_rich_layout.h"
#include "engine/gfx/gfx2d.h"
#include "engine/logging/log.h"

#include <math.h>
#include <string.h>
#include <stddef.h>

static int text_rich_utf8_decode_bounded(const char **p,
                                         const char *end,
                                         text_codepoint_t *out_codepoint)
{
    const unsigned char *s;
    ptrdiff_t remaining;

    if (!p || !*p || !out_codepoint)
        return 0;

    if (!end || *p >= end)
        return 0;

    s = (const unsigned char *)(*p);
    remaining = (ptrdiff_t)(end - *p);

    if (remaining <= 0 || s[0] == '\0')
        return 0;

    if (s[0] < 0x80) {
        *out_codepoint = (text_codepoint_t)s[0];
        *p += 1;
        return 1;
    }

    /* Reject stray continuation bytes, overlong 2-byte leads, and invalid lead bytes. */
    if (s[0] < 0xC2) {
        *out_codepoint = 0xFFFDu;
        *p += 1;
        return 1;
    }

    /* 2-byte sequence: C2..DF 80..BF */
    if (s[0] <= 0xDF) {
        if (remaining < 2 || (s[1] & 0xC0) != 0x80) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        *out_codepoint =
            ((text_codepoint_t)(s[0] & 0x1F) << 6) |
            ((text_codepoint_t)(s[1] & 0x3F));

        *p += 2;
        return 1;
    }

    /* 3-byte sequence: E0..EF with overlong/surrogate checks */
    if (s[0] <= 0xEF) {
        if (remaining < 3 ||
            (s[1] & 0xC0) != 0x80 ||
            (s[2] & 0xC0) != 0x80) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        /* Overlong: E0 80..9F */
        if (s[0] == 0xE0 && s[1] < 0xA0) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        /* Surrogates: ED A0..BF */
        if (s[0] == 0xED && s[1] >= 0xA0) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        *out_codepoint =
            ((text_codepoint_t)(s[0] & 0x0F) << 12) |
            ((text_codepoint_t)(s[1] & 0x3F) << 6) |
            ((text_codepoint_t)(s[2] & 0x3F));

        *p += 3;
        return 1;
    }

    /* 4-byte sequence: F0..F4 with overlong/out-of-range checks */
    if (s[0] <= 0xF4) {
        if (remaining < 4 ||
            (s[1] & 0xC0) != 0x80 ||
            (s[2] & 0xC0) != 0x80 ||
            (s[3] & 0xC0) != 0x80) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        /* Overlong: F0 80..8F */
        if (s[0] == 0xF0 && s[1] < 0x90) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        /* > U+10FFFF: F4 90..BF */
        if (s[0] == 0xF4 && s[1] > 0x8F) {
            *out_codepoint = 0xFFFDu;
            *p += 1;
            return 1;
        }

        *out_codepoint =
            ((text_codepoint_t)(s[0] & 0x07) << 18) |
            ((text_codepoint_t)(s[1] & 0x3F) << 12) |
            ((text_codepoint_t)(s[2] & 0x3F) << 6) |
            ((text_codepoint_t)(s[3] & 0x3F));

        *p += 4;
        return 1;
    }

    *out_codepoint = 0xFFFDu;
    *p += 1;
    return 1;
}

static int text_rich_is_space_cp(text_codepoint_t cp)
{
    return (cp == ' ' || cp == '\t');
}

static float text_rich_noise_01(int seed, float t)
{
    float v = sinf((float)seed * 12.9898f + t * 78.233f) * 43758.5453f;
    return v - floorf(v);
}

void text_rich_layout_init(text_rich_layout_t *layout,
                           text_layout_item_t *item_buffer,
                           unsigned short item_capacity,
                           text_layout_line_t *line_buffer,
                           unsigned short line_capacity)
{
    if (!layout)
        return;

    memset(layout, 0, sizeof(*layout));
    layout->items = item_buffer;
    layout->item_capacity = item_capacity;
    layout->lines = line_buffer;
    layout->line_capacity = line_capacity;
}

void text_rich_layout_reset(text_rich_layout_t *layout)
{
    if (!layout)
        return;

    layout->item_count = 0;
    layout->line_count = 0;
    layout->reveal_group_count = 0;
    layout->width = 0;
    layout->height = 0;
}

int text_rich_layout_build_plain(text_rich_layout_t *layout,
                                 const text_font_t *font,
                                 const text_rich_run_t *runs,
                                 unsigned short run_count,
                                 const text_layout_params_t *params,
                                 text_reveal_mode_t reveal_mode)
{
    unsigned short run_index;
    short pen_x;
    short pen_y;
    short line_origin_x;
    short line_width;
    text_codepoint_t prev_codepoint;
    int in_word;
    unsigned short current_group;

    if (!layout || !font || !runs || !params) {
        LOGLN("[text_rich] build_plain: invalid args");
        return -1;
    }

    if (!layout->items || !layout->lines) {
        LOGLN("[text_rich] build_plain: missing buffers");
        return -1;
    }

    if (layout->line_capacity == 0) {
        LOGLN("[text_rich] build_plain: line_capacity=0");
        return -1;
    }

    text_rich_layout_reset(layout);

    pen_x = params->origin_x;
    pen_y = params->origin_y;
    line_origin_x = params->origin_x;
    line_width = 0;
    prev_codepoint = 0;
    in_word = 0;
    current_group = 0;

    layout->line_count = 1;
    layout->lines[0].first_glyph = 0;
    layout->lines[0].glyph_count = 0;
    layout->lines[0].width = 0;

    for (run_index = 0; run_index < run_count; ++run_index) {
        const text_rich_run_t *run = &runs[run_index];
        const char *p = run->text_start;

        if (!run->text_start || !run->text_end || run->text_start > run->text_end) {
            LOGLN("[text_rich] build_plain: invalid run bounds index=%u",
                (unsigned int)run_index);
                return -1;
        }

        if (run->text_start == run->text_end)
            continue;

        while (p < run->text_end) {
            text_codepoint_t cp;
            const text_glyph_t *glyph;
            text_layout_item_t *item;

            if (!text_rich_utf8_decode_bounded(&p, run->text_end, &cp)) {
                LOGLN("[text_rich] build_plain: utf8 decode failed");
                return -1;
            }

            if (cp == '\r')
                continue;

            if (cp == '\n') {
                if (line_width > layout->width)
                    layout->width = line_width;

                layout->lines[layout->line_count - 1].width = line_width;

                if (layout->line_count >= layout->line_capacity) {
                    LOGLN("[text_rich] build_plain: line capacity exceeded");
                    return -1;
                }

                pen_x = line_origin_x;
                pen_y += font->line_height;
                line_width = 0;
                prev_codepoint = 0;
                in_word = 0;

                layout->lines[layout->line_count].first_glyph = layout->item_count;
                layout->lines[layout->line_count].glyph_count = 0;
                layout->lines[layout->line_count].width = 0;
                layout->line_count++;
                continue;
            }

            glyph = text_font_find_glyph(font, cp);
            if (!glyph)
                continue;

            if (layout->item_count >= layout->item_capacity) {
                LOGLN("[text_rich] build_plain: item capacity exceeded");
                return -1;
            }

            pen_x += text_font_get_kerning(font, prev_codepoint, cp);

            /* In TEXT_REVEAL_WORD mode, whitespace items are emitted using the current
               group so that spacing appears together with the preceding revealed word. */
            if (reveal_mode == TEXT_REVEAL_GLYPH) {
                current_group = layout->reveal_group_count++;
            } else {
                if (text_rich_is_space_cp(cp)) {
                    in_word = 0;
                } else if (!in_word) {
                    current_group = layout->reveal_group_count++;
                    in_word = 1;
                }
            }

            item = &layout->items[layout->item_count++];
            item->glyph = glyph;
            item->codepoint = cp;
            item->x = (short)(pen_x + glyph->xoffset);
            item->y = (short)(pen_y + glyph->yoffset);
            item->color = run->style.color;
            item->layer = params->style.layer;
            item->visible = 1;
            item->reveal_group = current_group;
            item->fx_flags = run->style.fx_flags;
            item->shake_amp_x = run->style.shake_amp_x;
            item->shake_amp_y = run->style.shake_amp_y;
            item->shake_speed = run->style.shake_speed;

            pen_x += glyph->xadvance + params->style.tracking;
            line_width = (short)(pen_x - line_origin_x);
            prev_codepoint = cp;

            layout->lines[layout->line_count - 1].glyph_count++;
            layout->lines[layout->line_count - 1].width = line_width;
        }
    }

    if (line_width > layout->width)
        layout->width = line_width;

    if (layout->line_count > 0)
        layout->lines[layout->line_count - 1].width = line_width;

    layout->height = (short)(layout->line_count * font->line_height);

    LOGLN("[text_rich] build_plain: ok items=%u lines=%u groups=%u width=%d height=%d",
          (unsigned int)layout->item_count,
          (unsigned int)layout->line_count,
          (unsigned int)layout->reveal_group_count,
          (int)layout->width,
          (int)layout->height);

    return 0;
}

void text_rich_draw_layout(const text_font_t *font,
                           const text_rich_layout_t *layout,
                           const text_reveal_state_t *reveal,
                           float time_seconds)
{
    unsigned short i;
    unsigned short visible_groups;

    if (!font || !layout)
        return;

    if (font->tex_id < 0)
        return;

    if (!layout->items)
        return;

    if (layout->item_count > layout->item_capacity)
        return;

    if (!layout->lines && layout->line_count > 0)
        return;

    visible_groups = layout->reveal_group_count;

    if (reveal && reveal->visible_units < visible_groups)
        visible_groups = reveal->visible_units;

    for (i = 0; i < layout->item_count; ++i) {
        const text_layout_item_t *item = &layout->items[i];
        const text_glyph_t *glyph = item->glyph;
        gfx2d_draw_params_t params;
        float draw_x;
        float draw_y;

        if (!glyph || !item->visible)
            continue;

        if (layout->reveal_group_count > 0 && item->reveal_group >= visible_groups)
            continue;

        draw_x = (float)item->x;
        draw_y = (float)item->y;

        if (item->fx_flags & TEXT_FX_SHAKE) {
            float nx = text_rich_noise_01((int)i * 2 + 0, time_seconds * item->shake_speed) * 2.0f - 1.0f;
            float ny = text_rich_noise_01((int)i * 2 + 1, time_seconds * item->shake_speed) * 2.0f - 1.0f;
            draw_x += nx * item->shake_amp_x;
            draw_y += ny * item->shake_amp_y;
        }

        params = gfx2d_sprite_params(draw_x,
                                     draw_y,
                                     (float)glyph->atlas_w,
                                     (float)glyph->atlas_h);

        params.layer = item->layer;

        params.anchor_h = GFX2D_HALIGN_LEFT;
        params.anchor_v = GFX2D_VALIGN_TOP;
        params.origin_h = GFX2D_HALIGN_LEFT;
        params.origin_v = GFX2D_VALIGN_TOP;
        params.skew_origin_h = GFX2D_HALIGN_LEFT;
        params.skew_origin_v = GFX2D_VALIGN_TOP;

        params.color.r = item->color.r;
        params.color.g = item->color.g;
        params.color.b = item->color.b;
        params.color.a = item->color.a;

        gfx2d_draw_texture_region(font->tex_id,
                                  &params,
                                  (float)glyph->atlas_x,
                                  (float)glyph->atlas_y,
                                  (float)glyph->atlas_w,
                                  (float)glyph->atlas_h);
    }
}