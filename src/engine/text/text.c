#include "engine/text/text.h"
#include "engine/logging/log.h"

#include <string.h>

typedef struct text_token_info {
    const char *start;
    const char *end;
    short width;
    unsigned short glyph_count;
    int is_space;
    int is_newline;
} text_token_info_t;

void text_font_init(text_font_t *font)
{
    if (!font)
        return;

    memset(font, 0, sizeof(*font));
    font->texture = gfx_texture_invalid();
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

text_color_t text_color_default(void)
{
    return text_color_rgba(0xFF, 0xFF, 0xFF, 0xFF);
}

void text_style_init(text_style_t *style)
{
    if (!style)
        return;

    style->color = text_color_default();
    style->tracking = 0;
    style->layer = 100;
    style->scale = 1.0f;
}

void text_reveal_state_init(text_reveal_state_t *state, float seconds_per_unit)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_units = 0;
    state->seconds_per_unit = seconds_per_unit;
    state->speed_scale = 1.0f;
}

void text_reveal_state_reset(text_reveal_state_t *state)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_units = 0;
}

void text_reveal_state_finish(text_reveal_state_t *state, unsigned short total_units)
{
    if (!state)
        return;

    state->reveal_timer = 0.0f;
    state->visible_units = total_units;
}

void text_reveal_state_set_speed_scale(text_reveal_state_t *state, float scale)
{
    if (!state)
        return;

    if (scale <= 0.0f)
        scale = 1.0f;

    state->speed_scale = scale;
}

void text_reveal_state_update(text_reveal_state_t *state,
                              unsigned short total_units,
                              float dt)
{
    float step;

    if (!state)
        return;

    step = state->seconds_per_unit;
    if (step <= 0.0f) {
        state->visible_units = total_units;
        return;
    }

    if (state->speed_scale > 0.0f)
        step /= state->speed_scale;

    state->reveal_timer += dt;

    while (state->visible_units < total_units &&
           state->reveal_timer >= step) {
        state->reveal_timer -= step;
        state->visible_units++;
    }
}