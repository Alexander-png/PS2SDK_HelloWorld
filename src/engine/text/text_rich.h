#ifndef TEXT_RICH_H
#define TEXT_RICH_H

#include "engine/text/text.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TEXT_RICH_STYLE_STACK_CAPACITY
#define TEXT_RICH_STYLE_STACK_CAPACITY 16
#endif

typedef enum text_reveal_mode {
    TEXT_REVEAL_GLYPH = 0,
    TEXT_REVEAL_WORD  = 1
} text_reveal_mode_t;

typedef enum text_fx_flags {
    TEXT_FX_NONE  = 0,
    TEXT_FX_SHAKE = 1 << 0
} text_fx_flags_t;

typedef struct text_rich_style {
    text_color_t color;
    unsigned short fx_flags;
    float shake_amp_x;
    float shake_amp_y;
    float shake_speed;
} text_rich_style_t;

typedef struct text_rich_run {
    const char *text_start;
    const char *text_end;
    text_rich_style_t style;
} text_rich_run_t;

void text_rich_style_init(text_rich_style_t *style);

int text_rich_parse(const char *markup_utf8,
                    const text_rich_style_t *base_style,
                    text_rich_run_t *runs,
                    unsigned short run_capacity,
                    unsigned short *out_run_count);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RICH_H */