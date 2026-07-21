#ifndef TEXT_RICH_LAYOUT_H
#define TEXT_RICH_LAYOUT_H

#include "engine/text/text.h"
#include "game/text/text_rich.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum text_layout_item_kind {
    TEXT_LAYOUT_ITEM_GLYPH = 0,
    TEXT_LAYOUT_ITEM_SPRITE
} text_layout_item_kind_t;

typedef struct text_layout_item {
    text_layout_item_kind_t kind;

    const text_glyph_t *glyph;
    text_codepoint_t codepoint;

    int sprite_tex_id;
    unsigned short sprite_u;
    unsigned short sprite_v;
    unsigned short sprite_w;
    unsigned short sprite_h;
    short sprite_xoffset;
    short sprite_yoffset;
    short sprite_xadvance;

    short x;
    short y;

    text_color_t color;
    int layer;
    unsigned char visible;

    float scale;

    unsigned short reveal_group;
    unsigned int glyph_index;

    text_rich_effect_params_t effects;
    unsigned int effect_seed;
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

typedef struct text_rich_draw_params {
    float shake_amp_scale_x;
    float shake_amp_scale_y;
    float shake_speed_scale;

    float wave_amp_scale_x;
    float wave_amp_scale_y;
    float wave_speed_scale;
} text_rich_draw_params_t;

void text_rich_layout_init(text_rich_layout_t *layout,
                           text_layout_item_t *item_buffer,
                           unsigned short item_capacity,
                           text_layout_line_t *line_buffer,
                           unsigned short line_capacity);

void text_rich_draw_params_init(text_rich_draw_params_t *params);                           

void text_rich_layout_reset(text_rich_layout_t *layout);

int text_rich_layout_build(text_rich_layout_t *layout,
                                 const text_font_t *font,
                                 const text_rich_run_t *runs,
                                 unsigned short run_count,
                                 const text_layout_params_t *params,
                                 text_reveal_mode_t reveal_mode);

void text_rich_draw_layout_ex(const text_font_t *font,
                              const text_rich_layout_t *layout,
                              const text_reveal_state_t *reveal,
                              float time_seconds,
                              const text_rich_draw_params_t *draw_params);                           

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RICH_LAYOUT_H */