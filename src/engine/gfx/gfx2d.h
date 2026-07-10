#ifndef GFX2D_H
#define GFX2D_H

#include <tamtypes.h>

#ifndef GFX2D_MAX_TEXTURES
#define GFX2D_MAX_TEXTURES 16
#endif

#ifndef GFX2D_MAX_SPRITES
#define GFX2D_MAX_SPRITES 32
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
    float w;
    float h;

    int layer;

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

    gfx2d_halign_t skew_origin_h;
    gfx2d_valign_t skew_origin_v;

    gfx2d_corner_t top_left;
    gfx2d_corner_t top_right;
    gfx2d_corner_t bottom_left;
    gfx2d_corner_t bottom_right;

    gfx2d_color_t color;
} gfx2d_draw_params_t;

int  gfx2d_init(void);
void gfx2d_shutdown(void);

void gfx2d_set_enabled(int enabled);
int  gfx2d_is_enabled(void);

void gfx2d_begin_frame(void);
void gfx2d_end_frame(void);

int  gfx2d_create_texture_from_png_data(const void *data, u32 size, int *out_tex_id);
int  gfx2d_touch_texture(int tex_id);
/* Frees CPU-side texture data and removes any sprites that reference it.
   VRAM residency is managed dynamically by gsKit_TexManager. */
void gfx2d_free_texture(int tex_id);

int  gfx2d_add_sprite(int tex_id, const gfx2d_draw_params_t *params, int *out_sprite_id);

int  gfx2d_draw_texture_region(int tex_id,
                               const gfx2d_draw_params_t *params,
                               float src_x,
                               float src_y,
                               float src_w,
                               float src_h);

int  gfx2d_texture_size(int tex_id, int *out_w, int *out_h);

int  gfx2d_update_sprite(int sprite_id, const gfx2d_draw_params_t *params);
int  gfx2d_set_sprite_texture(int sprite_id, int tex_id);
/* Sprite removal only affects sprite instances.
   Textures are persistent resources and must be freed separately. */
void gfx2d_remove_sprite(int sprite_id);
void gfx2d_clear_sprites(void);

gfx2d_draw_params_t gfx2d_sprite_params(float x, float y, float w, float h);

void gfx2d_draw(void);

#ifdef __cplusplus
}
#endif

#endif