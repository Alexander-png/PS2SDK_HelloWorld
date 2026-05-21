#ifndef GFX2D_H
#define GFX2D_H

#ifndef GFX2D_MAX_TEXTURES
#define GFX2D_MAX_TEXTURES 16
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gfx2d_halign {
    GFX2D_HALIGN_LEFT = 0,
    GFX2D_HALIGN_CENTER,
    GFX2D_HALIGN_RIGHT
} gfx2d_halign_t;

typedef enum gfx2d_valign {
    GFX2D_VALIGN_TOP = 0,
    GFX2D_VALIGN_CENTER,
    GFX2D_VALIGN_BOTTOM
} gfx2d_valign_t;

typedef struct gfx2d_color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} gfx2d_color_t;

typedef struct gfx2d_corner {
    float x;
    float y;
    float z;
    float u;
    float v;
} gfx2d_corner_t;

typedef struct gfx2d_draw_params {
    float x;
    float y;
    float z;
    float w;
    float h;

    float origin_x;
    float origin_y;

    float scale_x;
    float scale_y;

    float rotation_rad;
    float skew_x_rad;
    float skew_y_rad;

    int flip_x;
    int flip_y;

    gfx2d_halign_t origin_h;
    gfx2d_valign_t origin_v;

    gfx2d_halign_t anchor_h;
    gfx2d_valign_t anchor_v;

    gfx2d_corner_t top_left;
    gfx2d_corner_t top_right;
    gfx2d_corner_t bottom_left;
    gfx2d_corner_t bottom_right;

    gfx2d_color_t color;
} gfx2d_draw_params_t;

int  gfx2d_init(void);
void gfx2d_shutdown(void);

void gfx2d_begin_frame(void);
void gfx2d_end_frame(void);

int  gfx2d_load_texture(const char *path, int *out_tex_id);
void gfx2d_free_texture(int tex_id);

gfx2d_draw_params_t gfx2d_sprite_params(float x, float y, float w, float h);

void gfx2d_draw(int tex_id, const gfx2d_draw_params_t *params);

#ifdef __cplusplus
}
#endif

#endif