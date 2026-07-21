#include "game/text/text_rich.h"

#include <string.h>
#include <stddef.h>

typedef enum text_rich_tag_kind {
    TEXT_RICH_TAG_NONE = 0,
    TEXT_RICH_TAG_OPEN_COLOR,
    TEXT_RICH_TAG_CLOSE_COLOR,
    TEXT_RICH_TAG_OPEN_SHAKE,
    TEXT_RICH_TAG_CLOSE_SHAKE,
    TEXT_RICH_TAG_OPEN_WAVE,
    TEXT_RICH_TAG_CLOSE_WAVE
} text_rich_tag_kind_t;

typedef struct text_rich_tag {
    text_rich_tag_kind_t kind;
    text_color_t color;
} text_rich_tag_t;

typedef struct text_rich_style_frame {
    text_rich_tag_kind_t kind;
    text_rich_style_t style;
} text_rich_style_frame_t;

static int text_rich_hex_nibble(char c, unsigned char *out)
{
    if (!out)
        return -1;

    if (c >= '0' && c <= '9') {
        *out = (unsigned char)(c - '0');
        return 0;
    }

    if (c >= 'a' && c <= 'f') {
        *out = (unsigned char)(10 + (c - 'a'));
        return 0;
    }

    if (c >= 'A' && c <= 'F') {
        *out = (unsigned char)(10 + (c - 'A'));
        return 0;
    }

    return -1;
}

static int text_rich_hex_byte(const char *p, unsigned char *out)
{
    unsigned char hi;
    unsigned char lo;

    if (!p || !out)
        return -1;

    if (text_rich_hex_nibble(p[0], &hi) != 0)
        return -1;
    if (text_rich_hex_nibble(p[1], &lo) != 0)
        return -1;

    *out = (unsigned char)((hi << 4) | lo);
    return 0;
}

static int text_rich_parse_color_tag(const char *tag_start,
                                     const char *tag_end,
                                     text_rich_tag_t *out_tag)
{
    ptrdiff_t len;
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;

    if (!tag_start || !tag_end || !out_tag)
        return -1;

    len = (ptrdiff_t)(tag_end - tag_start);

    if (len == 7 && strncmp(tag_start, "/color]", 7) == 0) {
        out_tag->kind = TEXT_RICH_TAG_CLOSE_COLOR;
        out_tag->color = text_color_default();
        return 0;
    }

    if (strncmp(tag_start, "color=#", 7) != 0)
        return -1;

    if (len != 14 && len != 16)
        return -1;

    if (tag_end[-1] != ']')
        return -1;

    if (text_rich_hex_byte(tag_start + 7,  &r) != 0)
        return -1;
    if (text_rich_hex_byte(tag_start + 9,  &g) != 0)
        return -1;
    if (text_rich_hex_byte(tag_start + 11, &b) != 0)
        return -1;

    if (len == 16) {
        if (text_rich_hex_byte(tag_start + 13, &a) != 0)
            return -1;
    } else {
        a = 0xFF;
    }

    out_tag->kind = TEXT_RICH_TAG_OPEN_COLOR;
    out_tag->color = text_color_rgba(r, g, b, a);
    return 0;
}

static int text_rich_parse_shake_tag(const char *tag_start,
                                     const char *tag_end,
                                     text_rich_tag_t *out_tag)
{
    if (!tag_start || !tag_end || !out_tag)
        return -1;

    if ((tag_end - tag_start) == 6 &&
        strncmp(tag_start, "shake]", 6) == 0) {
        out_tag->kind = TEXT_RICH_TAG_OPEN_SHAKE;
        out_tag->color = text_color_default();
        return 0;
    }

    if ((tag_end - tag_start) == 7 &&
        strncmp(tag_start, "/shake]", 7) == 0) {
        out_tag->kind = TEXT_RICH_TAG_CLOSE_SHAKE;
        out_tag->color = text_color_default();
        return 0;
    }

    return -1;
}

static int text_rich_parse_wave_tag(const char *tag_start,
                                    const char *tag_end,
                                    text_rich_tag_t *out_tag)
{
    if (!tag_start || !tag_end || !out_tag)
        return -1;

    if ((tag_end - tag_start) == 5 &&
        strncmp(tag_start, "wave]", 5) == 0) {
        out_tag->kind = TEXT_RICH_TAG_OPEN_WAVE;
        out_tag->color = text_color_default();
        return 0;
    }

    if ((tag_end - tag_start) == 6 &&
        strncmp(tag_start, "/wave]", 6) == 0) {
        out_tag->kind = TEXT_RICH_TAG_CLOSE_WAVE;
        out_tag->color = text_color_default();
        return 0;
    }

    return -1;
}

static int text_rich_parse_tag(const char *tag_start,
                               const char *tag_end,
                               text_rich_tag_t *out_tag)
{
    if (!tag_start || !tag_end || !out_tag)
        return -1;

    if (text_rich_parse_color_tag(tag_start, tag_end, out_tag) == 0)
        return 0;

    if (text_rich_parse_shake_tag(tag_start, tag_end, out_tag) == 0)
        return 0;

    if (text_rich_parse_wave_tag(tag_start, tag_end, out_tag) == 0)
        return 0;

    return -1;
}

static int text_rich_emit_run(text_rich_run_t *runs,
                              unsigned short run_capacity,
                              unsigned short *run_count,
                              const char *start,
                              const char *end,
                              const text_rich_style_t *style)
{
    text_rich_run_t *run;

    if (!run_count || !style)
        return -1;

    if (!start || !end || start >= end)
        return 0;

    if (*run_count >= run_capacity)
        return -1;

    run = &runs[*run_count];
    run->text_start = start;
    run->text_end = end;
    run->style = *style;
    (*run_count)++;

    return 0;
}

void text_rich_style_init(text_rich_style_t *style)
{
    if (!style)
        return;

    style->color = text_color_default();
    style->scale = 1.0f;
    text_rich_effect_params_init(&style->effects);
}

void text_rich_effect_params_init(text_rich_effect_params_t *params)
{
    if (!params)
        return;

    params->flags = TEXT_RICH_EFFECT_NONE;
    params->amp_x = 0.0f;
    params->amp_y = 0.0f;
    params->speed = 0.0f;
    params->phase = 0.0f;
}

void text_rich_effect_params_apply_defaults(text_rich_effect_params_t *params)
{
    if (!params)
        return;

    if (params->flags & TEXT_RICH_EFFECT_SHAKE) {
        if (params->amp_x <= 0.0f)
            params->amp_x = 1.0f;
        if (params->amp_y <= 0.0f)
            params->amp_y = 1.0f;
        if (params->speed <= 0.0f)
            params->speed = 18.0f;
    }

    if (params->flags & TEXT_RICH_EFFECT_WAVE) {
        if (params->amp_x <= 0.0f)
            params->amp_x = 3.0f;
        if (params->amp_y <= 0.0f)
            params->amp_y = 3.0f;
        if (params->speed <= 0.0f)
            params->speed = 6.0f;
    }
}

// Not used for now
void text_rich_effect_params_merge(text_rich_effect_params_t *dst, 
                                   const text_rich_effect_params_t *src)
{
    if (!dst || !src)
        return;

    dst->flags |= src->flags;

    if (src->amp_x > 0.0f)
        dst->amp_x = src->amp_x;
    if (src->amp_y > 0.0f)
        dst->amp_y = src->amp_y;
    if (src->speed > 0.0f)
        dst->speed = src->speed;

    dst->phase = src->phase;
}

int text_rich_parse(const char *markup_utf8,
                    const text_rich_style_t *base_style,
                    text_rich_run_t *runs,
                    unsigned short run_capacity,
                    unsigned short *out_run_count)
{
    text_rich_style_frame_t style_stack[TEXT_RICH_STYLE_STACK_CAPACITY];
    unsigned short stack_size;
    unsigned short run_count;
    const char *p;
    const char *segment_start;

    if (!markup_utf8 || !base_style || !runs || !out_run_count)
        return -1;

    *out_run_count = 0;

    stack_size = 1;
    style_stack[0].kind = TEXT_RICH_TAG_NONE;
    style_stack[0].style = *base_style;

    run_count = 0;
    p = markup_utf8;
    segment_start = p;

    while (*p) {
        const char *tag_close;
        text_rich_tag_t tag;

        if (*p != '[') {
            ++p;
            continue;
        }

        tag_close = strchr(p + 1, ']');
        if (!tag_close) {
            ++p;
            continue;
        }

        if (text_rich_parse_tag(p + 1, tag_close + 1, &tag) != 0) {
            ++p;
            continue;
        }

        /* Only consume a closing tag if it matches the current top-of-stack tag.
           Otherwise treat it as literal text by advancing one byte and retrying. */
        if (tag.kind == TEXT_RICH_TAG_CLOSE_COLOR) {
            if (!(stack_size > 1 &&
                style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_COLOR)) {
                ++p;
                continue;
            }
        }

        if (tag.kind == TEXT_RICH_TAG_CLOSE_SHAKE) {
            if (!(stack_size > 1 &&
                style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_SHAKE)) {
                ++p;
                continue;
            }
        }

        if (tag.kind == TEXT_RICH_TAG_CLOSE_WAVE) {
            if (!(stack_size > 1 &&
                style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_WAVE)) {
                ++p;
                continue;
            }
        }

        if (text_rich_emit_run(runs,
            run_capacity,
            &run_count,
            segment_start,
            p,
            &style_stack[stack_size - 1].style) != 0) {
                return -1;
        }

        switch (tag.kind) {
            case TEXT_RICH_TAG_OPEN_COLOR:
                if (stack_size >= TEXT_RICH_STYLE_STACK_CAPACITY)
                    return -1;
                style_stack[stack_size] = style_stack[stack_size - 1];
                style_stack[stack_size].kind = TEXT_RICH_TAG_OPEN_COLOR;
                style_stack[stack_size].style.color = tag.color;
                stack_size++;
                break;

            case TEXT_RICH_TAG_CLOSE_COLOR:
                if (stack_size > 1 &&
                    style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_COLOR) {
                    stack_size--;
                }
            break;

            case TEXT_RICH_TAG_OPEN_SHAKE:
                if (stack_size >= TEXT_RICH_STYLE_STACK_CAPACITY)
                    return -1;
                style_stack[stack_size] = style_stack[stack_size - 1];
                style_stack[stack_size].kind = TEXT_RICH_TAG_OPEN_SHAKE;
                style_stack[stack_size].style.effects.flags |= TEXT_RICH_EFFECT_SHAKE;
                text_rich_effect_params_apply_defaults(&style_stack[stack_size].style.effects);
                stack_size++;
                break;

            case TEXT_RICH_TAG_CLOSE_SHAKE:
                if (stack_size > 1 &&
                    style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_SHAKE) {
                    stack_size--;
                }
                break;

            case TEXT_RICH_TAG_OPEN_WAVE:
                if (stack_size >= TEXT_RICH_STYLE_STACK_CAPACITY)
                    return -1;
                style_stack[stack_size] = style_stack[stack_size - 1];
                style_stack[stack_size].kind = TEXT_RICH_TAG_OPEN_WAVE;
                style_stack[stack_size].style.effects.flags |= TEXT_RICH_EFFECT_WAVE;
                text_rich_effect_params_apply_defaults(&style_stack[stack_size].style.effects);
                stack_size++;
                break;

            case TEXT_RICH_TAG_CLOSE_WAVE:
                if (stack_size > 1 &&
                    style_stack[stack_size - 1].kind == TEXT_RICH_TAG_OPEN_WAVE) {
                    stack_size--;
                }
                break;

            default:
                break;
        }

        p = tag_close + 1;
        segment_start = p;
    }

    if (text_rich_emit_run(runs,
                           run_capacity,
                           &run_count,
                           segment_start,
                           p,
                           &style_stack[stack_size - 1].style) != 0) {
        return -1;
    }

    *out_run_count = run_count;
    return 0;
}