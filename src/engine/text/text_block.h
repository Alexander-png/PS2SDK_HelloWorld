#ifndef TEXT_BLOCK_H
#define TEXT_BLOCK_H

#include "engine/text/text.h"
#include "engine/text/text_rich.h"
#include "engine/text/text_rich_layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum text_align_h {
    TEXT_ALIGN_LEFT = 0,
    TEXT_ALIGN_CENTER = 1,
    TEXT_ALIGN_RIGHT = 2
} text_align_h_t;

typedef enum text_align_v {
    TEXT_ALIGN_TOP = 0,
    TEXT_ALIGN_MIDDLE = 1,
    TEXT_ALIGN_BOTTOM = 2
} text_align_v_t;

typedef struct text_block {
    /* Font and box */
    const text_font_t *font;

    short box_x;
    short box_y;
    short box_w;
    short box_h;

    text_align_h_t align_h;
    text_align_v_t align_v;

    /* Style / params */
    text_style_t base_style;
    text_wrap_mode_t wrap_mode;
    text_reveal_mode_t reveal_mode;

    float reveal_seconds_per_unit;

    /* Reveal/animation state */
    text_reveal_state_t reveal;
    float time_seconds;

    float shake_amp_scale_x;
    float shake_amp_scale_y;
    float shake_speed_scale;

    float wave_amp_scale_x;
    float wave_amp_scale_y;
    float wave_speed_scale;

    float reveal_speed_scale;

    const char *text_utf8;

    /* Rich parse buffer */
    text_rich_run_t *runs;
    unsigned short run_count;
    unsigned short run_capacity;

    /* Rich layout */
    text_rich_layout_t layout;
    text_layout_item_t *items;
    unsigned short item_capacity;
    text_layout_line_t *lines;
    unsigned short line_capacity;
} text_block_t;

void text_block_init(text_block_t *blk,
                     text_rich_run_t *runs,
                     unsigned short run_capacity,
                     text_layout_item_t *items,
                     unsigned short item_capacity,
                     text_layout_line_t *lines,
                     unsigned short line_capacity,
                     float reveal_seconds_per_unit);

void text_block_set_font(text_block_t *tb, const text_font_t *font);
void text_block_set_text(text_block_t *tb, const char *markup_utf8);
void text_block_set_reveal_mode(text_block_t *tb, text_reveal_mode_t mode);

void text_block_set_origin(text_block_t *tb, short x, short y);
void text_block_set_box(text_block_t *tb, short x, short y, short w, short h);
void text_block_set_align_h(text_block_t *tb, text_align_h_t align_h);
void text_block_set_align_v(text_block_t *tb, text_align_v_t align_v);
void text_block_set_wrap_mode(text_block_t *tb, text_wrap_mode_t wrap_mode);
void text_block_set_shake_scale(text_block_t *tb, float amp_scale);
void text_block_set_shake_scale_xy(text_block_t *tb, float amp_scale_x, float amp_scale_y);
void text_block_set_shake_speed_scale(text_block_t *tb, float speed_scale);
void text_block_reset_rich_draw_params(text_block_t *tb);

void text_block_set_reveal_speed_scale(text_block_t *tb, float speed_scale);

void text_block_set_wave_scale(text_block_t *tb, float amp_scale);
void text_block_set_wave_scale_xy(text_block_t *tb, float amp_scale_x, float amp_scale_y);
void text_block_set_wave_speed_scale(text_block_t *tb, float speed_scale);

void text_block_set_style(text_block_t *tb, const text_style_t *style);

int text_block_refresh(text_block_t *tb);

short text_block_width(const text_block_t *tb);
short text_block_height(const text_block_t *tb);

void text_block_reveal_reset(text_block_t *tb);
void text_block_reveal_finish(text_block_t *tb);
void text_block_update(text_block_t *tb, float dt);

void text_block_draw(const text_block_t *tb);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_BLOCK_H */