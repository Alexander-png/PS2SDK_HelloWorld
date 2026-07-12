#ifndef TEXT_RICH_LAYOUT_H
#define TEXT_RICH_LAYOUT_H

#include "engine/text/text.h"
#include "engine/text/text_rich.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct text_layout_item {
    const text_glyph_t *glyph;
    text_codepoint_t codepoint;

    short x;
    short y;

    text_color_t color;
    int layer;
    unsigned char visible;

    unsigned short reveal_group;

    unsigned short fx_flags;
    float shake_amp_x;
    float shake_amp_y;
    float shake_speed;
} text_layout_item_t;

typedef struct text_rich_layout {
    text_layout_item_t *items;
    unsigned short item_count;
    unsigned short item_capacity;

    text_layout_line_t *lines;
    unsigned short line_count;
    unsigned short line_capacity;

    unsigned short reveal_group_count;

    short width;
    short height;
} text_rich_layout_t;

void text_rich_layout_init(text_rich_layout_t *layout,
                           text_layout_item_t *item_buffer,
                           unsigned short item_capacity,
                           text_layout_line_t *line_buffer,
                           unsigned short line_capacity);

void text_rich_layout_reset(text_rich_layout_t *layout);

int text_rich_layout_build_plain(text_rich_layout_t *layout,
                                 const text_font_t *font,
                                 const text_rich_run_t *runs,
                                 unsigned short run_count,
                                 const text_layout_params_t *params,
                                 text_reveal_mode_t reveal_mode);

void text_rich_draw_layout(const text_font_t *font,
                           const text_rich_layout_t *layout,
                           const text_reveal_state_t *reveal,
                           float time_seconds);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RICH_LAYOUT_H */