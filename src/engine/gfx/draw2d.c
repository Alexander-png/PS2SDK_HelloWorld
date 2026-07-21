#include "engine/gfx/draw2d.h"
#include "engine/gfx/internal/renderer_internal.h"
#include "engine/gfx/internal/texture_internal.h"

#include <gsKit.h>
#include <math.h>
#include <string.h>

typedef struct gfx_corner {
    float x;
    float y;
    float z;
    float u;
    float v;
} gfx_corner_t;

static float gfx_draw2d_resolve_halign(gfx_halign_t align, float w)
{
    switch (align) {
        case GFX_HALIGN_LEFT:   return 0.0f;
        case GFX_HALIGN_CENTER: return w * 0.5f;
        case GFX_HALIGN_RIGHT:  return w;
        default:                return 0.0f;
    }
}

static float gfx_draw2d_resolve_valign(gfx_valign_t align, float h)
{
    switch (align) {
        case GFX_VALIGN_TOP:    return 0.0f;
        case GFX_VALIGN_CENTER: return h * 0.5f;
        case GFX_VALIGN_BOTTOM: return h;
        default:                return 0.0f;
    }
}

static u8 gfx_draw2d_modulate_to_gs(u8 value)
{
    return (u8)(((int)value * 128 + 127) / 255);
}

static u64 gfx_draw2d_make_rgbaq(gfx_color_t color)
{
    return GS_SETREG_RGBAQ(
        gfx_draw2d_modulate_to_gs(color.r),
        gfx_draw2d_modulate_to_gs(color.g),
        gfx_draw2d_modulate_to_gs(color.b),
        gfx_draw2d_modulate_to_gs(color.a),
        0x00
    );
}

static void gfx_draw2d_transform_corner(const gfx_draw_params_t *params,
                                        float pivot_x,
                                        float pivot_y,
                                        float skew_pivot_x,
                                        float skew_pivot_y,
                                        float local_x,
                                        float local_y,
                                        gfx_corner_t *out)
{
    float sx = tanf(params->skew_x_rad);
    float sy = tanf(params->skew_y_rad);
    float x = local_x;
    float y = local_y;
    float c, s;
    float rx, ry;

    x -= skew_pivot_x;
    y -= skew_pivot_y;
    {
        float skewed_x = x + sx * y;
        float skewed_y = y + sy * x;
        x = skewed_x;
        y = skewed_y;
    }
    x += skew_pivot_x;
    y += skew_pivot_y;

    x -= pivot_x;
    y -= pivot_y;

    x *= params->scale_x;
    y *= params->scale_y;

    c = cosf(params->rotation_rad);
    s = sinf(params->rotation_rad);

    rx = x * c - y * s;
    ry = x * s + y * c;

    out->x = params->x + rx;
    out->y = params->y + ry;
    out->z = (float)params->layer;
}

static int gfx_draw2d_quad(GSTEXTURE *tex,
                           const gfx_draw_params_t *params,
                           float u_left,
                           float v_top,
                           float u_right,
                           float v_bottom)
{
    GSGLOBAL *gs = renderer_ps2_gs();
    gfx_corner_t top_left, top_right, bottom_left, bottom_right;
    float anchor_x, anchor_y;
    float pivot_x, pivot_y;
    float skew_pivot_x, skew_pivot_y;
    float local_left, local_top, local_right, local_bottom;

    if (!gs || !tex || !params)
        return -1;

    if (!tex->Mem)
        return -1;

    if (params->w == 0.0f || params->h == 0.0f)
        return 0;

    anchor_x = gfx_draw2d_resolve_halign(params->anchor_h, params->w);
    anchor_y = gfx_draw2d_resolve_valign(params->anchor_v, params->h);

    local_left   = -anchor_x;
    local_top    = -anchor_y;
    local_right  = local_left + params->w;
    local_bottom = local_top + params->h;

    pivot_x = gfx_draw2d_resolve_halign(params->origin_h, params->w) - anchor_x;
    pivot_y = gfx_draw2d_resolve_valign(params->origin_v, params->h) - anchor_y;

    skew_pivot_x = gfx_draw2d_resolve_halign(params->skew_origin_h, params->w) - anchor_x;
    skew_pivot_y = gfx_draw2d_resolve_valign(params->skew_origin_v, params->h) - anchor_y;

    gfx_draw2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                                local_left, local_top, &top_left);
    gfx_draw2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                                local_right, local_top, &top_right);
    gfx_draw2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                                local_left, local_bottom, &bottom_left);
    gfx_draw2d_transform_corner(params, pivot_x, pivot_y, skew_pivot_x, skew_pivot_y,
                                local_right, local_bottom, &bottom_right);

    top_left.u = u_left;
    top_left.v = v_top;

    top_right.u = u_right;
    top_right.v = v_top;

    bottom_left.u = u_left;
    bottom_left.v = v_bottom;

    bottom_right.u = u_right;
    bottom_right.v = v_bottom;

    gsKit_TexManager_bind(gs, tex);

    gsKit_prim_quad_texture_3d(
        gs,
        tex,
        top_left.x,     top_left.y,     top_left.z,     top_left.u,     top_left.v,
        top_right.x,    top_right.y,    top_right.z,    top_right.u,    top_right.v,
        bottom_left.x,  bottom_left.y,  bottom_left.z,  bottom_left.u,  bottom_left.v,
        bottom_right.x, bottom_right.y, bottom_right.z, bottom_right.u, bottom_right.v,
        gfx_draw2d_make_rgbaq(params->color)
    );

    return 0;
}

gfx_draw_params_t gfx_draw_params_default(float x, float y, float w, float h)
{
    gfx_draw_params_t p;

    memset(&p, 0, sizeof(p));

    p.x = x;
    p.y = y;
    p.w = w;
    p.h = h;
    p.layer = 0;

    p.scale_x = 1.0f;
    p.scale_y = 1.0f;

    p.rotation_rad = 0.0f;
    p.skew_x_rad = 0.0f;
    p.skew_y_rad = 0.0f;

    p.flip_x = 0;
    p.flip_y = 0;

    p.origin_h = GFX_HALIGN_CENTER;
    p.origin_v = GFX_VALIGN_CENTER;

    p.anchor_h = GFX_HALIGN_CENTER;
    p.anchor_v = GFX_VALIGN_CENTER;

    p.skew_origin_h = GFX_HALIGN_CENTER;
    p.skew_origin_v = GFX_VALIGN_CENTER;

    p.color.r = 0xFF;
    p.color.g = 0xFF;
    p.color.b = 0xFF;
    p.color.a = 0xFF;

    return p;
}

int gfx_draw_texture(gfx_texture_handle_t texture,
                     const gfx_draw_params_t *params)
{
    GSTEXTURE *tex;
    float u_left, u_right, v_top, v_bottom;

    if (!params)
        return -1;

    if (gfx_texture_get_gstexture_internal(texture, &tex) != 0)
        return -1;

    if (!tex || !tex->Mem)
        return -1;

    u_left   = params->flip_x ? (float)tex->Width  : 0.0f;
    u_right  = params->flip_x ? 0.0f               : (float)tex->Width;
    v_top    = params->flip_y ? (float)tex->Height : 0.0f;
    v_bottom = params->flip_y ? 0.0f               : (float)tex->Height;

    return gfx_draw2d_quad(tex, params, u_left, v_top, u_right, v_bottom);
}

int gfx_draw_texture_region(gfx_texture_handle_t texture,
                            const gfx_draw_params_t *params,
                            const gfx_rect_t *src_rect)
{
    GSTEXTURE *tex;
    float u_left, u_right, v_top, v_bottom;

    if (!params || !src_rect)
        return -1;

    if (src_rect->w <= 0.0f || src_rect->h <= 0.0f)
        return -1;

    if (gfx_texture_get_gstexture_internal(texture, &tex) != 0)
        return -1;

    if (!tex || !tex->Mem)
        return -1;

    u_left   = params->flip_x ? (src_rect->x + src_rect->w) : src_rect->x;
    u_right  = params->flip_x ? src_rect->x                 : (src_rect->x + src_rect->w);
    v_top    = params->flip_y ? (src_rect->y + src_rect->h) : src_rect->y;
    v_bottom = params->flip_y ? src_rect->y                 : (src_rect->y + src_rect->h);

    return gfx_draw2d_quad(tex, params, u_left, v_top, u_right, v_bottom);
}