#include "engine/text/text.h"
#include "engine/gfx/gfx2d.h"
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

static int text_layout_advance_line(text_layout_t *layout,
                                    const text_font_t *font,
                                    short line_origin_x,
                                    short *pen_x,
                                    short *pen_y,
                                    short *line_width,
                                    text_codepoint_t *prev_codepoint)
{
    if (!layout || !font || !pen_x || !pen_y || !line_width || !prev_codepoint)
        return -1;

    if (layout->line_count >= layout->line_capacity) {
        LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
              (unsigned int)layout->line_count,
              (unsigned int)layout->line_capacity);
        return -1;
    }

    *pen_x = line_origin_x;
    *pen_y += font->line_height;
    *line_width = 0;
    *prev_codepoint = 0;

    layout->lines[layout->line_count].first_glyph = layout->glyph_count;
    layout->lines[layout->line_count].glyph_count = 0;
    layout->lines[layout->line_count].width = 0;
    layout->line_count++;

    return 0;
}

static void text_layout_expand_line_width_to_glyph(text_layout_t *layout,
                                                   short line_origin_x,
                                                   const text_layout_glyph_t *out_glyph,
                                                   const text_glyph_t *glyph)
{
    text_layout_line_t *line;
    short glyph_right;
    short glyph_line_width;

    if (!layout || !out_glyph || !glyph || layout->line_count == 0)
        return;

    line = &layout->lines[layout->line_count - 1];
    glyph_right = (short)(out_glyph->x + glyph->atlas_w);
    glyph_line_width = (short)(glyph_right - line_origin_x);

    if (glyph_line_width > line->width)
        line->width = glyph_line_width;

    if (glyph_line_width > layout->width)
        layout->width = glyph_line_width;
}

static int text_measure_token(const text_font_t *font,
                              const char *start,
                              const char *end,
                              const text_layout_params_t *params,
                              text_codepoint_t *last_cp_out,
                              text_token_info_t *out_info)
{
    const char *p;
    text_codepoint_t prev_cp = 0;
    short width = 0;
    unsigned short glyph_count = 0;

    if (!font || !start || !end || !params || !out_info)
        return -1;

    p = start;

    while (p < end && *p) {
        text_codepoint_t cp;
        const text_glyph_t *glyph;

        if (!text_utf8_decode(&p, &cp))
            return -1;

        if (cp == '\r')
            continue;

        if (cp == '\n')
            break;

        glyph = text_font_find_glyph(font, cp);
        if (!glyph)
            continue;

        width += text_font_get_kerning(font, prev_cp, cp);
        width += glyph->xadvance + params->style.tracking;
        prev_cp = cp;
        glyph_count++;
    }

    out_info->start = start;
    out_info->end = end;
    out_info->width = width;
    out_info->glyph_count = glyph_count;
    out_info->is_space = 0;
    out_info->is_newline = 0;

    if (last_cp_out)
        *last_cp_out = prev_cp;

    return 0;
}

static int text_emit_token(text_layout_t *layout,
                           const text_font_t *font,
                           const text_token_info_t *token,
                           short *pen_x,
                           short pen_y,
                           short line_origin_x,
                           text_codepoint_t *prev_codepoint,
                           const text_layout_params_t *params)
{
    const char *p;

    if (!layout || !font || !token || !pen_x || !prev_codepoint || !params)
        return -1;

    p = token->start;

    while (p < token->end && *p) {
        text_codepoint_t cp;
        const text_glyph_t *glyph;
        text_layout_glyph_t *out_glyph;

        if (!text_utf8_decode(&p, &cp))
            return -1;

        if (cp == '\r')
            continue;

        if (cp == '\n')
            break;

        glyph = text_font_find_glyph(font, cp);
        if (!glyph)
            continue;

        if (layout->glyph_count >= layout->glyph_capacity) {
            LOGLNC(LOGCAT_TEXT, "[text] build_boxed: glyph capacity exceeded count=%u capacity=%u",
                  (unsigned int)layout->glyph_count,
                  (unsigned int)layout->glyph_capacity);
            return -1;
        }

        *pen_x += text_font_get_kerning(font, *prev_codepoint, cp);

        out_glyph = &layout->glyphs[layout->glyph_count++];
        out_glyph->glyph = glyph;
        out_glyph->codepoint = cp;
        out_glyph->x = (short)(*pen_x + glyph->xoffset);
        out_glyph->y = (short)(pen_y + glyph->yoffset);
        out_glyph->color = params->style.color;
        out_glyph->layer = params->style.layer;
        out_glyph->visible = 1;

        text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

        *pen_x += glyph->xadvance + params->style.tracking;
        *prev_codepoint = cp;

        layout->lines[layout->line_count - 1].glyph_count++;
    }

    return 0;
}

static int text_emit_token_char_wrapped(text_layout_t *layout,
                                        const text_font_t *font,
                                        const char **p_inout,
                                        const char *end,
                                        short *pen_x,
                                        short pen_y,
                                        short line_origin_x,
                                        short max_width,
                                        text_codepoint_t *prev_codepoint,
                                        const text_layout_params_t *params)
{
    const char *p;
    int emitted_any = 0;

    if (!layout || !font || !p_inout || !*p_inout || !pen_x || !prev_codepoint || !params)
        return -1;

    p = *p_inout;

    while (p < end && *p) {
        const char *next = p;
        text_codepoint_t cp;
        const text_glyph_t *glyph;
        short test_pen_x;
        text_layout_glyph_t *out_glyph;

        if (!text_utf8_decode(&next, &cp))
            return -1;

        if (cp == '\r') {
            p = next;
            continue;
        }

        if (cp == '\n')
            break;

        glyph = text_font_find_glyph(font, cp);
        if (!glyph) {
            p = next;
            continue;
        }

        test_pen_x = *pen_x;
        test_pen_x += text_font_get_kerning(font, *prev_codepoint, cp);

        {
            short glyph_right = (short)(test_pen_x + glyph->xoffset + glyph->atlas_w);
            short glyph_line_width = (short)(glyph_right - line_origin_x);

            if (max_width > 0 &&
                glyph_line_width > max_width &&
                layout->lines[layout->line_count - 1].glyph_count > 0) {
                break;
            }
        }

        if (layout->glyph_count >= layout->glyph_capacity) {
            LOGLNC(LOGCAT_TEXT, "[text] build_boxed: glyph capacity exceeded count=%u capacity=%u",
                  (unsigned int)layout->glyph_count,
                  (unsigned int)layout->glyph_capacity);
            return -1;
        }

        *pen_x += text_font_get_kerning(font, *prev_codepoint, cp);

        out_glyph = &layout->glyphs[layout->glyph_count++];
        out_glyph->glyph = glyph;
        out_glyph->codepoint = cp;
        out_glyph->x = (short)(*pen_x + glyph->xoffset);
        out_glyph->y = (short)(pen_y + glyph->yoffset);
        out_glyph->color = params->style.color;
        out_glyph->layer = params->style.layer;
        out_glyph->visible = 1;

        text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

        *pen_x += glyph->xadvance + params->style.tracking;
        *prev_codepoint = cp;
        layout->lines[layout->line_count - 1].glyph_count++;

        emitted_any = 1;
        p = next;
    }

    if (!emitted_any && p < end && *p) {
        const char *next = p;
        text_codepoint_t cp;
        const text_glyph_t *glyph;
        text_layout_glyph_t *out_glyph;

        if (!text_utf8_decode(&next, &cp))
            return -1;

        if (cp != '\r' && cp != '\n') {
            glyph = text_font_find_glyph(font, cp);
            if (glyph) {
                if (layout->glyph_count >= layout->glyph_capacity) {
                    LOGLNC(LOGCAT_TEXT, "[text] build_boxed: glyph capacity exceeded count=%u capacity=%u",
                          (unsigned int)layout->glyph_count,
                          (unsigned int)layout->glyph_capacity);
                    return -1;
                }

                *pen_x += text_font_get_kerning(font, *prev_codepoint, cp);

                out_glyph = &layout->glyphs[layout->glyph_count++];
                out_glyph->glyph = glyph;
                out_glyph->codepoint = cp;
                out_glyph->x = (short)(*pen_x + glyph->xoffset);
                out_glyph->y = (short)(pen_y + glyph->yoffset);
                out_glyph->color = params->style.color;
                out_glyph->layer = params->style.layer;
                out_glyph->visible = 1;

                text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

                *pen_x += glyph->xadvance + params->style.tracking;
                *prev_codepoint = cp;
                layout->lines[layout->line_count - 1].glyph_count++;

                p = next;
            } else {
                p = next;
            }
        } else {
            p = next;
        }
    }

    *p_inout = p;
    return 0;
}

static int text_layout_next_token(const char **p_in, text_token_info_t *out_token)
{
    const char *p;
    const char *start;

    if (!p_in || !*p_in || !out_token)
        return 0;

    p = *p_in;
    if (*p == '\0')
        return 0;

    memset(out_token, 0, sizeof(*out_token));
    start = p;

    if (*p == '\n') {
        out_token->start = p;
        out_token->end = p + 1;
        out_token->is_newline = 1;
        *p_in = p + 1;
        return 1;
    }

    if (*p == '\r') {
        out_token->start = p;
        out_token->end = p + 1;
        *p_in = p + 1;
        return 1;
    }

    if (*p == ' ' || *p == '\t') {
        while (*p == ' ' || *p == '\t')
            ++p;

        out_token->start = start;
        out_token->end = p;
        out_token->is_space = 1;
        *p_in = p;
        return 1;
    }

    while (*p && *p != '\n' && *p != '\r' && *p != ' ' && *p != '\t')
        ++p;

    out_token->start = start;
    out_token->end = p;
    *p_in = p;
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

    if (!layout || !font || !utf8_text || !params) {
        LOGLNC(LOGCAT_TEXT, "[text] build_plain: invalid args layout=%p font=%p text=%p params=%p",
              layout, font, utf8_text, params);
        return -1;
    }

    if (!layout->glyphs || !layout->lines) {
        LOGLNC(LOGCAT_TEXT, "[text] build_plain: missing buffers glyphs=%p lines=%p",
              layout->glyphs, layout->lines);
        return -1;
    }

    if (layout->line_capacity == 0) {
        LOGLNC(LOGCAT_TEXT, "[text] build_plain: line_capacity=0");
        return -1;
    }

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

        if (!text_utf8_decode(&p, &cp)) {
            LOGLNC(LOGCAT_TEXT, "[text] build_plain: utf8 decode failed");
            break;
        }

        LOGLNC(LOGCAT_TEXT, "[text] build_plain: cp=U+%04X", (unsigned int)cp);

        if (cp == '\r')
            continue;

        if (cp == '\n') {
            if (line_width > layout->width)
                layout->width = line_width;

            layout->lines[layout->line_count - 1].width = line_width;

            if (layout->line_count >= layout->line_capacity) {
                LOGLNC(LOGCAT_TEXT, "[text] build_plain: line capacity exceeded count=%u capacity=%u",
                      (unsigned int)layout->line_count,
                      (unsigned int)layout->line_capacity);
                return -1;
            }

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
        if (!glyph) {
            LOGLNC(LOGCAT_TEXT, "[text] build_plain: glyph not found cp=U+%04X", (unsigned int)cp);
            continue;
        }

        if (layout->glyph_count >= layout->glyph_capacity) {
            LOGLNC(LOGCAT_TEXT, "[text] build_plain: glyph capacity exceeded count=%u capacity=%u",
                  (unsigned int)layout->glyph_count,
                  (unsigned int)layout->glyph_capacity);
            return -1;
        }

        pen_x += text_font_get_kerning(font, prev_codepoint, cp);

        out_glyph = &layout->glyphs[layout->glyph_count++];
        out_glyph->glyph = glyph;
        out_glyph->codepoint = cp;
        out_glyph->x = (short)(pen_x + glyph->xoffset);
        out_glyph->y = (short)(pen_y + glyph->yoffset);
        out_glyph->color = params->style.color;
        out_glyph->layer = params->style.layer;
        out_glyph->visible = 1;

        text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

        pen_x += glyph->xadvance + params->style.tracking;
        line_width = layout->lines[layout->line_count - 1].width;
        prev_codepoint = cp;

        layout->lines[layout->line_count - 1].glyph_count++;
    }

    if (line_width > layout->width)
        layout->width = line_width;

    if (layout->line_count > 0)
        layout->lines[layout->line_count - 1].width = line_width;

    layout->height = (short)(layout->line_count * font->line_height);

    LOGLNC(LOGCAT_TEXT, "[text] build_plain: ok glyphs=%u lines=%u width=%d height=%d",
          (unsigned int)layout->glyph_count,
          (unsigned int)layout->line_count,
          (int)layout->width,
          (int)layout->height);

    return 0;
}

static int text_layout_build_boxed_charwrap(text_layout_t *layout,
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
        return -1;

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
        const char *next = p;
        text_codepoint_t cp;
        const text_glyph_t *glyph;
        short test_pen_x;
        short glyph_right;
        short glyph_line_width;
        text_layout_glyph_t *out_glyph;

        if (!text_utf8_decode(&next, &cp))
            return -1;

        p = next;

        if (cp == '\r')
            continue;

        if (cp == '\n') {
            line_width = layout->lines[layout->line_count - 1].width;
            if (text_layout_advance_line(layout,
                                         font,
                                         line_origin_x,
                                         &pen_x,
                                         &pen_y,
                                         &line_width,
                                         &prev_codepoint) != 0) {
                return -1;
            }
            continue;
        }

        if (cp == ' ' || cp == '\t') {
            if (layout->lines[layout->line_count - 1].glyph_count == 0)
                continue;

            glyph = text_font_find_glyph(font, cp);
            if (!glyph)
                continue;

            test_pen_x = pen_x + text_font_get_kerning(font, prev_codepoint, cp);
            glyph_right = (short)(test_pen_x + glyph->xoffset + glyph->atlas_w);
            glyph_line_width = (short)(glyph_right - line_origin_x);

            if (params->max_width > 0 && glyph_line_width > params->max_width) {
                if (text_layout_advance_line(layout,
                                             font,
                                             line_origin_x,
                                             &pen_x,
                                             &pen_y,
                                             &line_width,
                                             &prev_codepoint) != 0) {
                    return -1;
                }
                continue;
            }

            if (layout->glyph_count >= layout->glyph_capacity) {
                LOGLNC(LOGCAT_TEXT, "[text] build_boxed_charwrap: glyph capacity exceeded count=%u capacity=%u",
                      (unsigned int)layout->glyph_count,
                      (unsigned int)layout->glyph_capacity);
                return -1;
            }

            pen_x += text_font_get_kerning(font, prev_codepoint, cp);

            out_glyph = &layout->glyphs[layout->glyph_count++];
            out_glyph->glyph = glyph;
            out_glyph->codepoint = cp;
            out_glyph->x = (short)(pen_x + glyph->xoffset);
            out_glyph->y = (short)(pen_y + glyph->yoffset);
            out_glyph->color = params->style.color;
            out_glyph->layer = params->style.layer;
            out_glyph->visible = 1;

            text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

            pen_x += glyph->xadvance + params->style.tracking;
            prev_codepoint = cp;
            layout->lines[layout->line_count - 1].glyph_count++;
            line_width = layout->lines[layout->line_count - 1].width;
            continue;
        }

        glyph = text_font_find_glyph(font, cp);
        if (!glyph)
            continue;

        test_pen_x = pen_x + text_font_get_kerning(font, prev_codepoint, cp);
        glyph_right = (short)(test_pen_x + glyph->xoffset + glyph->atlas_w);
        glyph_line_width = (short)(glyph_right - line_origin_x);

        if (params->max_width > 0 &&
            glyph_line_width > params->max_width &&
            layout->lines[layout->line_count - 1].glyph_count > 0) {
            if (text_layout_advance_line(layout,
                                         font,
                                         line_origin_x,
                                         &pen_x,
                                         &pen_y,
                                         &line_width,
                                         &prev_codepoint) != 0) {
                return -1;
            }

            test_pen_x = pen_x + text_font_get_kerning(font, prev_codepoint, cp);
        }

        if (layout->glyph_count >= layout->glyph_capacity) {
            LOGLNC(LOGCAT_TEXT, "[text] build_boxed_charwrap: glyph capacity exceeded count=%u capacity=%u",
                  (unsigned int)layout->glyph_count,
                  (unsigned int)layout->glyph_capacity);
            return -1;
        }

        pen_x += text_font_get_kerning(font, prev_codepoint, cp);

        out_glyph = &layout->glyphs[layout->glyph_count++];
        out_glyph->glyph = glyph;
        out_glyph->codepoint = cp;
        out_glyph->x = (short)(pen_x + glyph->xoffset);
        out_glyph->y = (short)(pen_y + glyph->yoffset);
        out_glyph->color = params->style.color;
        out_glyph->layer = params->style.layer;
        out_glyph->visible = 1;

        text_layout_expand_line_width_to_glyph(layout, line_origin_x, out_glyph, glyph);

        pen_x += glyph->xadvance + params->style.tracking;
        prev_codepoint = cp;
        layout->lines[layout->line_count - 1].glyph_count++;
        line_width = layout->lines[layout->line_count - 1].width;
    }

    if (line_width > layout->width)
        layout->width = line_width;

    if (layout->line_count > 0)
        layout->lines[layout->line_count - 1].width = line_width;

    layout->height = (short)(layout->line_count * font->line_height);
    
    // Commented due its noisy
    // LOGLNC(LOGCAT_TEXT, "[text] build_boxed_charwrap: ok glyphs=%u lines=%u width=%d height=%d",
    //       (unsigned int)layout->glyph_count,
    //       (unsigned int)layout->line_count,
    //       (int)layout->width,
    //       (int)layout->height);

    return 0;
}

int text_layout_build_boxed(text_layout_t *layout,
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
    const char *pending_space_start;
    const char *pending_space_end;
    int has_pending_space;

    if (!layout || !font || !utf8_text || !params) {
        LOGLNC(LOGCAT_TEXT, "[text] build_boxed: invalid args layout=%p font=%p text=%p params=%p",
              layout, font, utf8_text, params);
        return -1;
    }

    if (!layout->glyphs || !layout->lines) {
        LOGLNC(LOGCAT_TEXT, "[text] build_boxed: missing buffers glyphs=%p lines=%p",
              layout->glyphs, layout->lines);
        return -1;
    }

    if (layout->line_capacity == 0) {
        LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line_capacity=0");
        return -1;
    }

    // Commented due its noisy
    // LOGLNC(LOGCAT_TEXT, "[text] build_boxed: origin=(%d,%d) max_width=%d wrap_mode=%d",
    //     (int)params->origin_x,
    //     (int)params->origin_y,
    //     (int)params->max_width,
    //     (int)params->wrap_mode);

    text_layout_reset(layout);

    p = utf8_text;
    pen_x = params->origin_x;
    pen_y = params->origin_y;
    line_origin_x = params->origin_x;
    line_width = 0;
    prev_codepoint = 0;
    pending_space_start = NULL;
    pending_space_end = NULL;
    has_pending_space = 0;

    layout->line_count = 1;
    layout->lines[0].first_glyph = 0;
    layout->lines[0].glyph_count = 0;
    layout->lines[0].width = 0;

    while (*p) {
        text_token_info_t token;
        text_token_info_t measured;

        if (!text_layout_next_token(&p, &token))
            break;

        if (token.is_newline) {
            if (line_width > layout->width)
                layout->width = line_width;

            layout->lines[layout->line_count - 1].width = line_width;

            if (layout->line_count >= layout->line_capacity) {
                LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
                      (unsigned int)layout->line_count,
                      (unsigned int)layout->line_capacity);
                return -1;
            }

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

        if (token.is_space) {
            if (layout->lines[layout->line_count - 1].glyph_count > 0) {
                pending_space_start = token.start;
                pending_space_end = token.end;
                has_pending_space = 1;
            }
            continue;
        }

        if (text_measure_token(font, token.start, token.end, params, NULL, &measured) != 0)
            return -1;

        text_token_info_t pending_space_measured;
        int pending_space_width = 0;

        memset(&pending_space_measured, 0, sizeof(pending_space_measured));

        if (has_pending_space) {
            if (text_measure_token(font,
                pending_space_start,
                pending_space_end,
                params,
                NULL,
                &pending_space_measured) != 0) {
                return -1;
            }
            pending_space_width = pending_space_measured.width;
        }

        if (params->wrap_mode == TEXT_WRAP_CHAR) {
            return text_layout_build_boxed_charwrap(layout, font, utf8_text, params);
        }
        if (params->wrap_mode == TEXT_WRAP_WORD && params->max_width > 0) {
            int current_line_has_glyphs;
            int word_fits_empty_line;
            int word_with_space_fits_current;

            current_line_has_glyphs = (layout->lines[layout->line_count - 1].glyph_count > 0);
            word_fits_empty_line = (measured.width <= params->max_width);
            word_with_space_fits_current = ((short)(line_width +
                                                    (has_pending_space ? pending_space_width : 0) +
                                                    measured.width) <= params->max_width);

            if (!current_line_has_glyphs) {
                has_pending_space = 0;

                if (word_fits_empty_line) {
                    if (text_emit_token(layout,
                                        font,
                                        &measured,
                                        &pen_x,
                                        pen_y,
                                        line_origin_x,
                                        &prev_codepoint,
                                        params) != 0)
                        return -1;

                    line_width = layout->lines[layout->line_count - 1].width;
                    continue;
                }

                {
                    const char *emit_p = token.start;

                    while (emit_p < token.end) {
                        unsigned short glyphs_before;
                        short pen_x_before;

                        glyphs_before = layout->glyph_count;
                        pen_x_before = pen_x;

                        if (text_emit_token_char_wrapped(layout,
                                                        font,
                                                        &emit_p,
                                                        token.end,
                                                        &pen_x,
                                                        pen_y,
                                                        line_origin_x,
                                                        params->max_width,
                                                        &prev_codepoint,
                                                        params) != 0) {
                            return -1;
                        }

                        line_width = layout->lines[layout->line_count - 1].width;

                        if (emit_p < token.end) {
                            if (layout->line_count >= layout->line_capacity) {
                                LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
                                    (unsigned int)layout->line_count,
                                    (unsigned int)layout->line_capacity);
                                return -1;
                            }

                            if (layout->glyph_count == glyphs_before && pen_x == pen_x_before) {
                                LOGLNC(LOGCAT_TEXT, "[text] build_boxed: word fallback char wrap made no progress");
                                return -1;
                            }

                            pen_x = line_origin_x;
                            pen_y += font->line_height;
                            line_width = 0;
                            prev_codepoint = 0;

                            layout->lines[layout->line_count].first_glyph = layout->glyph_count;
                            layout->lines[layout->line_count].glyph_count = 0;
                            layout->lines[layout->line_count].width = 0;
                            layout->line_count++;
                        }
                    }

                    line_width = layout->lines[layout->line_count - 1].width;
                    continue;
                }
            }

            if (word_with_space_fits_current) {
                if (has_pending_space) {
                    if (text_emit_token(layout,
                                        font,
                                        &pending_space_measured,
                                        &pen_x,
                                        pen_y,
                                        line_origin_x,
                                        &prev_codepoint,
                                        params) != 0) {
                        return -1;
                    }

                    line_width = layout->lines[layout->line_count - 1].width;
                    has_pending_space = 0;
                }

                if (text_emit_token(layout,
                                    font,
                                    &measured,
                                    &pen_x,
                                    pen_y,
                                    line_origin_x,
                                    &prev_codepoint,
                                    params) != 0)
                    return -1;

                line_width = layout->lines[layout->line_count - 1].width;
                continue;
            }

            has_pending_space = 0;

            if (word_fits_empty_line) {
                if (layout->line_count >= layout->line_capacity) {
                    LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
                        (unsigned int)layout->line_count,
                        (unsigned int)layout->line_capacity);
                    return -1;
                }

                pen_x = line_origin_x;
                pen_y += font->line_height;
                line_width = 0;
                prev_codepoint = 0;

                layout->lines[layout->line_count].first_glyph = layout->glyph_count;
                layout->lines[layout->line_count].glyph_count = 0;
                layout->lines[layout->line_count].width = 0;
                layout->line_count++;

                if (text_emit_token(layout,
                                    font,
                                    &measured,
                                    &pen_x,
                                    pen_y,
                                    line_origin_x,
                                    &prev_codepoint,
                                    params) != 0)
                    return -1;

                line_width = layout->lines[layout->line_count - 1].width;
                continue;
            }

            if (layout->line_count >= layout->line_capacity) {
                LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
                    (unsigned int)layout->line_count,
                    (unsigned int)layout->line_capacity);
                return -1;
            }

            pen_x = line_origin_x;
            pen_y += font->line_height;
            line_width = 0;
            prev_codepoint = 0;

            layout->lines[layout->line_count].first_glyph = layout->glyph_count;
            layout->lines[layout->line_count].glyph_count = 0;
            layout->lines[layout->line_count].width = 0;
            layout->line_count++;

            {
                const char *emit_p = token.start;

                while (emit_p < token.end) {
                    unsigned short glyphs_before;
                    short pen_x_before;

                    glyphs_before = layout->glyph_count;
                    pen_x_before = pen_x;

                    if (text_emit_token_char_wrapped(layout,
                                                    font,
                                                    &emit_p,
                                                    token.end,
                                                    &pen_x,
                                                    pen_y,
                                                    line_origin_x,
                                                    params->max_width,
                                                    &prev_codepoint,
                                                    params) != 0) {
                        return -1;
                    }

                    line_width = layout->lines[layout->line_count - 1].width;

                    if (emit_p < token.end) {
                        if (layout->line_count >= layout->line_capacity) {
                            LOGLNC(LOGCAT_TEXT, "[text] build_boxed: line capacity exceeded count=%u capacity=%u",
                                (unsigned int)layout->line_count,
                                (unsigned int)layout->line_capacity);
                            return -1;
                        }

                        if (layout->glyph_count == glyphs_before && pen_x == pen_x_before) {
                            LOGLNC(LOGCAT_TEXT, "[text] build_boxed: word fallback char wrap made no progress");
                            return -1;
                        }

                        pen_x = line_origin_x;
                        pen_y += font->line_height;
                        line_width = 0;
                        prev_codepoint = 0;

                        layout->lines[layout->line_count].first_glyph = layout->glyph_count;
                        layout->lines[layout->line_count].glyph_count = 0;
                        layout->lines[layout->line_count].width = 0;
                        layout->line_count++;
                    }
                }

                line_width = layout->lines[layout->line_count - 1].width;
                continue;
            }
        }
    }

    if (line_width > layout->width)
        layout->width = line_width;

    if (layout->line_count > 0)
        layout->lines[layout->line_count - 1].width = line_width;

    layout->height = (short)(layout->line_count * font->line_height);

    // Commented due its noisy
    // LOGLNC(LOGCAT_TEXT, "[text] build_boxed: ok glyphs=%u lines=%u width=%d height=%d",
    //       (unsigned int)layout->glyph_count,
    //       (unsigned int)layout->line_count,
    //       (int)layout->width,
    //       (int)layout->height);

    return 0;
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

void text_draw_layout(const text_font_t *font,
                      const text_layout_t *layout,
                      const text_reveal_state_t *reveal)
{
    unsigned short i;
    unsigned short visible_units;

    if (!font || !layout)
        return;

    if (font->tex_id < 0)
        return;

    visible_units = layout->glyph_count;

    if (reveal && reveal->visible_units < visible_units)
        visible_units = reveal->visible_units;

    for (i = 0; i < visible_units; ++i) {
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