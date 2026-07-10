#include "engine/text/text.h"
#include "engine/gfx/gfx2d.h"

#include <string.h>

static int text_utf8_decode(const char **p, text_codepoint_t *out_codepoint)
{
    const unsigned char *s;

    if (!p || !*p || !out_codepoint)
        return 0;

    s = (const unsigned char *)(*p);

    if (s[0] == '\0')
        return 0;

    if (s[0] < 0x80) {
        *out_codepoint = s[0];
        *p += 1;
        return 1;
    }

    if ((s[0] & 0xE0) == 0xC0) {
        if ((s[1] & 0xC0) != 0x80) {
            *out_codepoint = '?';
            *p += 1;
            return 1;
        }

        *out_codepoint = ((text_codepoint_t)(s[0] & 0x1F) << 6) |
                         ((text_codepoint_t)(s[1] & 0x3F));
        *p += 2;
        return 1;
    }

    if ((s[0] & 0xF0) == 0xE0) {
        if (((s[1] & 0xC0) != 0x80) || ((s[2] & 0xC0) != 0x80)) {
            *out_codepoint = '?';
            *p += 1;
            return 1;
        }

        *out_codepoint = ((text_codepoint_t)(s[0] & 0x0F) << 12) |
                         ((text_codepoint_t)(s[1] & 0x3F) << 6) |
                         ((text_codepoint_t)(s[2] & 0x3F));
        *p += 3;
        return 1;
    }

    if ((s[0] & 0xF8) == 0xF0) {
        if (((s[1] & 0xC0) != 0x80) ||
            ((s[2] & 0xC0) != 0x80) ||
            ((s[3] & 0xC0) != 0x80)) {
            *out_codepoint = '?';
            *p += 1;
            return 1;
        }

        *out_codepoint = ((text_codepoint_t)(s[0] & 0x07) << 18) |
                         ((text_codepoint_t)(s[1] & 0x3F) << 12) |
                         ((text_codepoint_t)(s[2] & 0x3F) << 6) |
                         ((text_codepoint_t)(s[3] & 0x3F));
        *p += 4;
        return 1;
    }

    *out_codepoint = '?';
    *p += 1;
    return 1;
}


void text_font_init(text_font_t *font)
{
    if (!font)
        return;

    memset(font, 0, sizeof(*font));
    font->tex_id = -1;
    font->fallback_codepoint = '?';
}


void text_font_shutdown(text_font_t *font)
{
    (void)font;
}


const text_glyph_t *text_font_find_glyph(const text_font_t *font, text_codepoint_t codepoint)
{
    unsigned int i;
    const text_glyph_t *fallback;

    if (!font || !font->glyphs)
        return 0;

    fallback = 0;

    for (i = 0; i < font->glyph_count; ++i) {
        const text_glyph_t *glyph = &font->glyphs[i];

        if (!glyph->valid)
            continue;

        if (glyph->codepoint == codepoint)
            return glyph;

        if (glyph->codepoint == font->fallback_codepoint)
            fallback = glyph;
    }

    return fallback;
}


short text_font_get_kerning(const text_font_t *font,
                            text_codepoint_t first,
                            text_codepoint_t second)
{
    unsigned int i;

    if (!font || !font->kernings)
        return 0;

    for (i = 0; i < font->kerning_count; ++i) {
        const text_kerning_pair_t *k = &font->kernings[i];

        if (k->first == first && k->second == second)
            return k->amount;
    }

    return 0;
}


text_color_t text_color_rgba(unsigned char r,
                             unsigned char g,
                             unsigned char b,
                             unsigned char a)
{
    text_color_t color;

    color.r = r;
    color.g = g;
    color.b = b;
    color.a = a;

    return color;
}


text_color_t text_color_white(void)
{
    return text_color_rgba(0x80, 0x80, 0x80, 0x80);
}


text_color_t text_color_yellow(void)
{
    return text_color_rgba(0x80, 0x80, 0x00, 0x80);
}


void text_style_init(text_style_t *style)
{
    if (!style)
        return;

    style->color = text_color_white();
    style->tracking = 0;
    style->layer = 100;
}


void text_layout_init(text_layout_t *layout,
                      text_layout_glyph_t *glyph_buffer,
                      unsigned short glyph_capacity,
                      text_layout_line_t *line_buffer,
                      unsigned short line_capacity)
{
    if (!layout)
        return;

    memset(layout, 0, sizeof(*layout));
    layout->glyphs = glyph_buffer;
    layout->glyph_capacity = glyph_capacity;
    layout->lines = line_buffer;
    layout->line_capacity = line_capacity;
}


void text_layout_reset(text_layout_t *layout)
{
    if (!layout)
        return;

    layout->glyph_count = 0;
    layout->line_count = 0;
    layout->width = 0;
    layout->height = 0;
}


int text_layout_build_plain(text_layout_t *layout,
                            const text_font_t *font,
                            const char *utf8_text,
                            const text_layout_params_t *params)
{
    const char *p;
    short pen_x;
    short pen_y;
    short line_origin_x;
    short line_width;
    text_codepoint_t prev_codepoint;

    if (!layout || !font || !utf8_text || !params)
        return 0;

    if (!layout->glyphs || !layout->lines)
        return 0;

    if (layout->line_capacity == 0)
        return 0;

    text_layout_reset(layout);

    p = utf8_text;
    pen_x = params->origin_x;
    pen_y = params->origin_y;
    line_origin_x = params->origin_x;
    line_width = 0;
    prev_codepoint = 0;

    layout->line_count = 1;
    layout->lines[0].first_glyph = 0;
    layout->lines[0].glyph_count = 0;
    layout->lines[0].width = 0;

    while (*p) {
        text_codepoint_t cp;
        const text_glyph_t *glyph;
        text_layout_glyph_t *out_glyph;

        if (!text_utf8_decode(&p, &cp))
            break;

        if (cp == '\r')
            continue;

        if (cp == '\n') {
            if (line_width > layout->width)
                layout->width = line_width;

            layout->lines[layout->line_count - 1].width = line_width;

            if (layout->line_count >= layout->line_capacity)
                return 0;

            pen_x = line_origin_x;
            pen_y += font->line_height;
            line_width = 0;
            prev_codepoint = 0;

            layout->lines[layout->line_count].first_glyph = layout->glyph_count;
            layout->lines[layout->line_count].glyph_count = 0;
            layout->lines[layout->line_count].width = 0;
            layout->line_count++;
            continue;
        }

        glyph = text_font_find_glyph(font, cp);
        if (!glyph)
            continue;

        if (layout->glyph_count >= layout->glyph_capacity)
            return 0;

        pen_x += text_font_get_kerning(font, prev_codepoint, cp);

        out_glyph = &layout->glyphs[layout->glyph_count++];
        out_glyph->glyph = glyph;
        out_glyph->codepoint = cp;
        out_glyph->x = (short)(pen_x + glyph->xoffset);
        out_glyph->y = (short)(pen_y + glyph->yoffset);
        out_glyph->color = params->style.color;
        out_glyph->layer = params->style.layer;
        out_glyph->visible = 1;

        pen_x += glyph->xadvance + params->style.tracking;
        line_width = (short)(pen_x - line_origin_x);
        prev_codepoint = cp;

        layout->lines[layout->line_count - 1].glyph_count++;
    }

    if (line_width > layout->width)
        layout->width = line_width;

    if (layout->line_count > 0)
        layout->lines[layout->line_count - 1].width = line_width;

    layout->height = (short)(layout->line_count * font->line_height);

    return 1;
}


void text_reveal_state_init(text_reveal_state_t *state, float seconds_per_glyph)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_glyphs = 0;
    state->seconds_per_glyph = seconds_per_glyph;
}


void text_reveal_state_reset(text_reveal_state_t *state)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_glyphs = 0;
}


void text_reveal_state_finish(text_reveal_state_t *state, unsigned short total_glyphs)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_glyphs = total_glyphs;
}


void text_reveal_state_update(text_reveal_state_t *state,
                              unsigned short total_glyphs,
                              float dt)
{
    if (!state)
        return;

    if (state->seconds_per_glyph <= 0.0f) {
        state->visible_glyphs = total_glyphs;
        return;
    }

    state->reveal_timer += dt;

    while (state->visible_glyphs < total_glyphs &&
           state->reveal_timer >= state->seconds_per_glyph) {
        state->reveal_timer -= state->seconds_per_glyph;
        state->visible_glyphs++;
    }
}


void text_draw_layout(const text_font_t *font,
                      const text_layout_t *layout,
                      const text_reveal_state_t *reveal)
{
    unsigned short i;
    unsigned short visible_glyphs;

    if (!font || !layout)
        return;

    if (font->tex_id < 0)
        return;

    visible_glyphs = layout->glyph_count;

    if (reveal && reveal->visible_glyphs < visible_glyphs)
        visible_glyphs = reveal->visible_glyphs;

    for (i = 0; i < visible_glyphs; ++i) {
        const text_layout_glyph_t *g = &layout->glyphs[i];
        const text_glyph_t *glyph = g->glyph;
        gfx2d_draw_params_t params;

        if (!glyph || !g->visible)
            continue;

        params = gfx2d_sprite_params((float)g->x,
                                     (float)g->y,
                                     (float)glyph->atlas_w,
                                     (float)glyph->atlas_h);

        params.layer = g->layer;

        params.anchor_h = GFX2D_HALIGN_LEFT;
        params.anchor_v = GFX2D_VALIGN_TOP;
        params.origin_h = GFX2D_HALIGN_LEFT;
        params.origin_v = GFX2D_VALIGN_TOP;
        params.skew_origin_h = GFX2D_HALIGN_LEFT;
        params.skew_origin_v = GFX2D_VALIGN_TOP;

        params.color.r = g->color.r;
        params.color.g = g->color.g;
        params.color.b = g->color.b;
        params.color.a = g->color.a;

        gfx2d_draw_texture_region(font->tex_id,
                                  &params,
                                  (float)glyph->atlas_x,
                                  (float)glyph->atlas_y,
                                  (float)glyph->atlas_w,
                                  (float)glyph->atlas_h);
    }
}