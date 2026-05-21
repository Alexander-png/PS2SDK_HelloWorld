#include "engine/logging/log.h"
#include "gfx2d.h"

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

typedef struct texture_slot {
    int used;
    GSTEXTURE tex;
} texture_slot_t;

static GSGLOBAL *g_gs;
static texture_slot_t g_textures[GFX2D_MAX_TEXTURES];

static u64 g_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);

static u64 gfx2d_make_rgbaq(gfx2d_color_t color)
{
    return GS_SETREG_RGBAQ(color.r, color.g, color.b, color.a, 0x00);
}

static void gfx2d_draw_vertices(int tex_id,
                                const gfx2d_corner_t *top_left,
                                const gfx2d_corner_t *top_right,
                                const gfx2d_corner_t *bottom_left,
                                const gfx2d_corner_t *bottom_right,
                                u64 color)
{
    GSTEXTURE *tex;

    if (!g_gs)
        return;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    gsKit_prim_quad_texture_3d(
        g_gs,
        tex,
        top_left->x,     top_left->y,     top_left->z,     top_left->u,     top_left->v,
        top_right->x,    top_right->y,    top_right->z,    top_right->u,    top_right->v,
        bottom_left->x,  bottom_left->y,  bottom_left->z,  bottom_left->u,  bottom_left->v,
        bottom_right->x, bottom_right->y, bottom_right->z, bottom_right->u, bottom_right->v,
        color
    );
}

static void gfx2d_draw_sprite_internal(int tex_id, const gfx2d_draw_params_t *params)
{
    GSTEXTURE *tex;
    gfx2d_corner_t top_left, top_right, bottom_left, bottom_right;
    float c, s;
    float lx1, ly1, lx2, ly2, lx3, ly3, lx4, ly4;
    float u_left, u_right, v_top, v_bottom;

    if (params->w == 0.0f || params->h == 0.0f)
        return;

    tex = &g_textures[tex_id].tex;

    c = cosf(params->rotation_rad);
    s = sinf(params->rotation_rad);

    lx1 = -params->origin_x;
    ly1 = -params->origin_y;

    lx2 = params->w - params->origin_x;
    ly2 = -params->origin_y;

    lx3 = -params->origin_x;
    ly3 = params->h - params->origin_y;

    lx4 = params->w - params->origin_x;
    ly4 = params->h - params->origin_y;

    lx1 *= params->scale_x; ly1 *= params->scale_y;
    lx2 *= params->scale_x; ly2 *= params->scale_y;
    lx3 *= params->scale_x; ly3 *= params->scale_y;
    lx4 *= params->scale_x; ly4 *= params->scale_y;

    top_left.x = params->x + lx1 * c - ly1 * s;
    top_left.y = params->y + lx1 * s + ly1 * c;
    top_left.z = params->z;

    top_right.x = params->x + lx2 * c - ly2 * s;
    top_right.y = params->y + lx2 * s + ly2 * c;
    top_right.z = params->z;

    bottom_left.x = params->x + lx3 * c - ly3 * s;
    bottom_left.y = params->y + lx3 * s + ly3 * c;
    bottom_left.z = params->z;

    bottom_right.x = params->x + lx4 * c - ly4 * s;
    bottom_right.y = params->y + lx4 * s + ly4 * c;
    bottom_right.z = params->z;

    u_left   = params->flip_x ? (float)tex->Width  : 0.0f;
    u_right  = params->flip_x ? 0.0f               : (float)tex->Width;
    v_top    = params->flip_y ? (float)tex->Height : 0.0f;
    v_bottom = params->flip_y ? 0.0f               : (float)tex->Height;

    top_left.u = u_left;
    top_left.v = v_top;

    top_right.u = u_right;
    top_right.v = v_top;

    bottom_left.u = u_left;
    bottom_left.v = v_bottom;

    bottom_right.u = u_right;
    bottom_right.v = v_bottom;

    gfx2d_draw_vertices(tex_id,
                        &top_left,
                        &top_right,
                        &bottom_left,
                        &bottom_right,
                        gfx2d_make_rgbaq(params->color));
}

static void gfx2d_draw_freeform_internal(int tex_id, const gfx2d_draw_params_t *params)
{
    gfx2d_draw_vertices(tex_id,
                        &params->top_left,
                        &params->top_right,
                        &params->bottom_left,
                        &params->bottom_right,
                        gfx2d_make_rgbaq(params->color));
}

int gfx2d_init(void)
{
    g_gs = gsKit_init_global();
    if (!g_gs)
        return -1;

    g_gs->PSM  = GS_PSM_CT32;
    g_gs->PSMZ = GS_PSMZ_16S;
    g_gs->Mode = gsKit_check_rom();
    g_gs->Height = (g_gs->Mode == GS_MODE_PAL) ? 512 : 448;

    dmaKit_init(D_CTRL_RELE_OFF,
                D_CTRL_MFD_OFF,
                D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF,
                D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(g_gs);

    g_gs->PrimAlphaEnable = GS_SETTING_ON;

    gsKit_mode_switch(g_gs, GS_ONESHOT);

    memset(g_textures, 0, sizeof(g_textures));
    return 0;
}

void gfx2d_shutdown(void)
{
    int i;

    if (!g_gs)
        return;

    for (i = 0; i < GFX2D_MAX_TEXTURES; ++i) {
        if (g_textures[i].used)
            gfx2d_free_texture(i);
    }

    g_gs = NULL;
}

void gfx2d_begin_frame(void)
{
    int oldAlpha;

    if (!g_gs)
        return;

    oldAlpha = g_gs->PrimAlphaEnable;
    g_gs->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_clear(g_gs, g_clear_color);
    g_gs->PrimAlphaEnable = oldAlpha;
}

void gfx2d_end_frame(void)
{
    if (!g_gs)
        return;

    gsKit_queue_exec(g_gs);
    gsKit_sync_flip(g_gs);
}

int gfx2d_load_texture(const char *path, int *out_tex_id)
{
    int i;

    if (!g_gs || !path || !out_tex_id)
        return -1;

    *out_tex_id = -1;

    for (i = 0; i < GFX2D_MAX_TEXTURES; ++i) {
        if (!g_textures[i].used) {
            memset(&g_textures[i].tex, 0, sizeof(GSTEXTURE));

            if (gsKit_texture_png(g_gs, &g_textures[i].tex, (char*)path) < 0)
                return -1;

            g_textures[i].used = 1;
            *out_tex_id = i;
            return 0;
        }
    }

    return -1;
}

void gfx2d_free_texture(int tex_id)
{
    GSTEXTURE *tex;

    if (!g_gs
        || tex_id < 0
        || tex_id >= GFX2D_MAX_TEXTURES
        || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    if (tex->Mem) {
        free(tex->Mem);
        tex->Mem = NULL;
    }

    if (tex->Clut) {
        free(tex->Clut);
        tex->Clut = NULL;
    }

    tex->Vram = 0;
    tex->VramClut = 0;
    memset(tex, 0, sizeof(*tex));
    g_textures[tex_id].used = 0;
}

gfx2d_draw_params_t gfx2d_sprite_params(float x, float y, float w, float h)
{
    gfx2d_draw_params_t p;

    memset(&p, 0, sizeof(p));

    p.mode = GFX2D_DRAW_MODE_SPRITE;

    p.x = x;
    p.y = y;
    p.z = 0.0f;
    p.w = w;
    p.h = h;

    p.origin_x = 0.0f;
    p.origin_y = 0.0f;

    p.scale_x = 1.0f;
    p.scale_y = 1.0f;

    p.rotation_rad = 0.0f;

    p.flip_x = 0;
    p.flip_y = 0;

    p.color.r = 0x80;
    p.color.g = 0x80;
    p.color.b = 0x80;
    p.color.a = 0x80;

    return p;
}

gfx2d_draw_params_t gfx2d_freeform_params(float x, float y, float z, float w, float h)
{
    gfx2d_draw_params_t p;

    memset(&p, 0, sizeof(p));

    p.mode = GFX2D_DRAW_MODE_FREEFORM;

    p.top_left.x = x;
    p.top_left.y = y;
    p.top_left.z = z;
    p.top_left.u = 0.0f;
    p.top_left.v = 0.0f;

    p.top_right.x = x + w;
    p.top_right.y = y;
    p.top_right.z = z;
    p.top_right.u = w;
    p.top_right.v = 0.0f;

    p.bottom_left.x = x;
    p.bottom_left.y = y + h;
    p.bottom_left.z = z;
    p.bottom_left.u = 0.0f;
    p.bottom_left.v = h;

    p.bottom_right.x = x + w;
    p.bottom_right.y = y + h;
    p.bottom_right.z = z;
    p.bottom_right.u = w;
    p.bottom_right.v = h;

    p.color.r = 0x80;
    p.color.g = 0x80;
    p.color.b = 0x80;
    p.color.a = 0x80;

    return p;
}

void gfx2d_draw(int tex_id, const gfx2d_draw_params_t *params)
{
    if (!g_gs || !params)
        return;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    switch (params->mode) {
        case GFX2D_DRAW_MODE_SPRITE:
            gfx2d_draw_sprite_internal(tex_id, params);
            break;

        case GFX2D_DRAW_MODE_FREEFORM:
            gfx2d_draw_freeform_internal(tex_id, params);
            break;

        default:
            break;
    }
}