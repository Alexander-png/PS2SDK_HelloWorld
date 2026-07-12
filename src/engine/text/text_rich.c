#include "engine/text/text_rich.h"

#include <string.h>
#include <stddef.h>

typedef enum text_rich_tag_kind {
    TEXT_RICH_TAG_NONE = 0,
    TEXT_RICH_TAG_OPEN_COLOR,
    TEXT_RICH_TAG_CLOSE_COLOR,
    TEXT_RICH_TAG_OPEN_SHAKE,
    TEXT_RICH_TAG_CLOSE_SHAKE
} text_rich_tag_kind_t;

typedef struct text_rich_tag {
    text_rich_tag_kind_t kind;
    text_color_t color;
} text_rich_tag_t;

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
        out_tag->color = text_color_white();
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
        a = 0x80;
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
        out_tag->color = text_color_white();
        return 0;
    }

    if ((tag_end - tag_start) == 7 &&
        strncmp(tag_start, "/shake]", 7) == 0) {
        out_tag->kind = TEXT_RICH_TAG_CLOSE_SHAKE;
        out_tag->color = text_color_white();
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

    style->color = text_color_white();
    style->fx_flags = TEXT_FX_NONE;
    style->shake_amp_x = 1.0f;
    style->shake_amp_y = 1.0f;
    style->shake_speed = 18.0f;
}

int text_rich_parse(const char *markup_utf8,
                    const text_rich_style_t *base_style,
                    text_rich_run_t *runs,
                    unsigned short run_capacity,
                    unsigned short *out_run_count)
{
    text_rich_style_t style_stack[TEXT_RICH_STYLE_STACK_CAPACITY];
    unsigned short stack_size;
    unsigned short run_count;
    const char *p;
    const char *segment_start;

    if (!markup_utf8 || !base_style || !runs || !out_run_count)
        return -1;

    *out_run_count = 0;

    stack_size = 1;
    style_stack[0] = *base_style;

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

        if (text_rich_emit_run(runs,
                               run_capacity,
                               &run_count,
                               segment_start,
                               p,
                               &style_stack[stack_size - 1]) != 0) {
            return -1;
        }

        switch (tag.kind) {
            case TEXT_RICH_TAG_OPEN_COLOR:
                if (stack_size >= TEXT_RICH_STYLE_STACK_CAPACITY)
                    return -1;
                style_stack[stack_size] = style_stack[stack_size - 1];
                style_stack[stack_size].color = tag.color;
                stack_size++;
                break;

            case TEXT_RICH_TAG_CLOSE_COLOR:
                if (stack_size > 1)
                    stack_size--;
                break;

            case TEXT_RICH_TAG_OPEN_SHAKE:
                if (stack_size >= TEXT_RICH_STYLE_STACK_CAPACITY)
                    return -1;
                style_stack[stack_size] = style_stack[stack_size - 1];
                style_stack[stack_size].fx_flags |= TEXT_FX_SHAKE;
                stack_size++;
                break;

            case TEXT_RICH_TAG_CLOSE_SHAKE:
                if (stack_size > 1)
                    stack_size--;
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
                           &style_stack[stack_size - 1]) != 0) {
        return -1;
    }

    *out_run_count = run_count;
    return 0;
}