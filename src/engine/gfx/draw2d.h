#ifndef DRAW2D_H
#define DRAW2D_H

#include "engine/gfx/texture.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum gfx_halign {
    GFX_HALIGN_LEFT = 0,
    GFX_HALIGN_CENTER,
    GFX_HALIGN_RIGHT
} gfx_halign_t;

typedef enum gfx_valign {
    GFX_VALIGN_TOP = 0,
    GFX_VALIGN_CENTER,
    GFX_VALIGN_BOTTOM
} gfx_valign_t;

typedef struct gfx_color {
    unsigned char r;
    unsigned char g;
    unsigned char b;
    unsigned char a;
} gfx_color_t;

typedef struct gfx_draw_params {
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

    gfx_halign_t origin_h;
    gfx_valign_t origin_v;

    gfx_halign_t anchor_h;
    gfx_valign_t anchor_v;

    gfx_halign_t skew_origin_h;
    gfx_valign_t skew_origin_v;

    gfx_color_t color;
} gfx_draw_params_t;

typedef struct gfx_rect {
    float x;
    float y;
    float w;
    float h;
} gfx_rect_t;

gfx_draw_params_t gfx_draw_params_default(float x, float y, float w, float h);

int gfx_draw_texture(gfx_texture_handle_t texture,
                     const gfx_draw_params_t *params);

int gfx_draw_texture_region(gfx_texture_handle_t texture,
                            const gfx_draw_params_t *params,
                            const gfx_rect_t *src_rect);

#ifdef __cplusplus
}
#endif

#endif