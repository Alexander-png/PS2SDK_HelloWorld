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
    TEXT_REVEAL_NONE  = 0,
    TEXT_REVEAL_GLYPH = 1,
    TEXT_REVEAL_WORD  = 2
} text_reveal_mode_t;

typedef enum text_rich_effect_flags {
    TEXT_RICH_EFFECT_NONE    = 0,
    TEXT_RICH_EFFECT_SHAKE   = 1 << 0,
    TEXT_RICH_EFFECT_WAVE    = 1 << 1
} text_rich_effect_flags_t;

typedef struct text_rich_effect_params {
    unsigned int flags;

    float amp_x;
    float amp_y;
    float speed;
    float phase;
} text_rich_effect_params_t;

typedef struct text_rich_style {
    text_color_t color;
    text_rich_effect_params_t effects;
} text_rich_style_t;

typedef struct text_rich_run {
    const char *text_start;
    const char *text_end;
    text_rich_style_t style;
} text_rich_run_t;

void text_rich_style_init(text_rich_style_t *style);
void text_rich_effect_params_init(text_rich_effect_params_t *params);
void text_rich_effect_params_apply_defaults(text_rich_effect_params_t *params);
void text_rich_effect_params_merge(text_rich_effect_params_t *dst,
                                   const text_rich_effect_params_t *src);

int text_rich_parse(const char *markup_utf8,
                    const text_rich_style_t *base_style,
                    text_rich_run_t *runs,
                    unsigned short run_capacity,
                    unsigned short *out_run_count);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_RICH_H */