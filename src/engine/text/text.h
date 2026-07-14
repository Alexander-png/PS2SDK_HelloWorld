#ifndef TEXT_H
#define TEXT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int text_codepoint_t;

typedef struct text_color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} text_color_t;

typedef struct text_glyph {
    text_codepoint_t codepoint;

    unsigned short atlas_x;
    unsigned short atlas_y;
    unsigned short atlas_w;
    unsigned short atlas_h;

    short xoffset;
    short yoffset;
    short xadvance;

    unsigned char page;
    unsigned char valid;
} text_glyph_t;

typedef struct text_kerning_pair {
    text_codepoint_t first;
    text_codepoint_t second;
    short amount;
} text_kerning_pair_t;

typedef struct text_font {
    const char *name;

    unsigned short atlas_width;
    unsigned short atlas_height;
    unsigned short line_height;
    short base;

    int tex_id;

    text_glyph_t *glyphs;
    unsigned int glyph_count;

    text_kerning_pair_t *kernings;
    unsigned int kerning_count;

    text_codepoint_t fallback_codepoint;
} text_font_t;

typedef struct text_style {
    text_color_t color;
    short tracking;
    int layer;
} text_style_t;

typedef struct text_layout_glyph {
    const text_glyph_t *glyph;
    text_codepoint_t codepoint;

    short x;
    short y;

    text_color_t color;
    int layer;
    unsigned char visible;
} text_layout_glyph_t;

typedef struct text_layout_line {
    unsigned short first_glyph; // rename glyph to item
    unsigned short glyph_count;
    short width;
} text_layout_line_t;

typedef struct text_layout {
    text_layout_glyph_t *glyphs;
    unsigned short glyph_count;
    unsigned short glyph_capacity;

    text_layout_line_t *lines;
    unsigned short line_count;
    unsigned short line_capacity;

    short width;
    short height;
} text_layout_t;

typedef struct text_reveal_state {
    float reveal_timer;
    unsigned short visible_units;
    float seconds_per_unit;
    float speed_scale;
} text_reveal_state_t;

typedef enum text_wrap_mode {
    TEXT_WRAP_NONE = 0,
    TEXT_WRAP_WORD = 1,
    TEXT_WRAP_CHAR = 2
} text_wrap_mode_t;

typedef struct text_layout_params {
    short origin_x;
    short origin_y;
    short max_width;
    text_wrap_mode_t wrap_mode;
    text_style_t style;
} text_layout_params_t;

void text_font_init(text_font_t *font);
void text_font_shutdown(text_font_t *font);

const text_glyph_t *text_font_find_glyph(const text_font_t *font, text_codepoint_t codepoint);
short text_font_get_kerning(const text_font_t *font,
                            text_codepoint_t first,
                            text_codepoint_t second);

text_color_t text_color_rgba(unsigned char r,
                             unsigned char g,
                             unsigned char b,
                             unsigned char a);

text_color_t text_color_white(void);
text_color_t text_color_yellow(void);

void text_style_init(text_style_t *style);

void text_layout_init(text_layout_t *layout,
                      text_layout_glyph_t *glyph_buffer,
                      unsigned short glyph_capacity,
                      text_layout_line_t *line_buffer,
                      unsigned short line_capacity);

void text_layout_reset(text_layout_t *layout);

int  text_layout_build_plain(text_layout_t *layout,
                             const text_font_t *font,
                             const char *utf8_text,
                             const text_layout_params_t *params);

int  text_layout_build_boxed(text_layout_t *layout,
                             const text_font_t *font,
                             const char *utf8_text,
                             const text_layout_params_t *params);

void text_reveal_state_init(text_reveal_state_t *state, float seconds_per_glyph);
void text_reveal_state_reset(text_reveal_state_t *state);
void text_reveal_state_finish(text_reveal_state_t *state, unsigned short total_units);
void text_reveal_state_set_speed_scale(text_reveal_state_t *state, float scale);
void text_reveal_state_update(text_reveal_state_t *state,
                              unsigned short total_units,
                              float dt);

void text_draw_layout(const text_font_t *font,
                      const text_layout_t *layout,
                      const text_reveal_state_t *reveal);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_H */