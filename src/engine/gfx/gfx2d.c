#include "engine/logging/log.h"
#include "gfx2d.h"

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

// TODO:
// Move texture parameters into structure

typedef struct texture_slot {
    int used;
    GSTEXTURE tex;
} texture_slot_t;

static GSGLOBAL *g_gs;
static texture_slot_t g_textures[GFX2D_MAX_TEXTURES];

static u64 g_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);
static u64 g_modulate    = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);


static void gfx2d_draw_quad_uv(int tex_id,
                               float x1, float y1, float u1, float v1,
                               float x2, float y2, float u2, float v2,
                               float x3, float y3, float u3, float v3,
                               float x4, float y4, float u4, float v4)
{
    GSTEXTURE *tex;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    gsKit_prim_quad_texture_3d(
        g_gs,
        tex,
        x1, y1, 0, u1, v1,
        x2, y2, 0, u2, v2,
        x3, y3, 0, u3, v3,
        x4, y4, 0, u4, v4,
        g_modulate
    );
}

// ------------------- PUBLIC API -------------------------
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
    if (!g_gs)
        return;

    int i;

    for (i = 0; i < GFX2D_MAX_TEXTURES; ++i) {
        if (g_textures[i].used)
            gfx2d_free_texture(i);
    }

    g_gs = NULL;
}

void gfx2d_begin_frame(void)
{
    if (!g_gs)
        return;
    // Clear with blending disabled to guarantee full overwrite
    // Fixes bug with ghosting
    int oldAlpha = g_gs->PrimAlphaEnable;
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
    if (!g_gs
        || !path
        || !out_tex_id)
        return -1;

    *out_tex_id = -1;

    int i;

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
    if (!g_gs 
        || tex_id < 0
        || tex_id >= GFX2D_MAX_TEXTURES 
        || !g_textures[tex_id].used)
        return;
    
    GSTEXTURE *tex;

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

void gfx2d_draw_sprite(int tex_id, float x, float y, float w, float h)
{
    if (!g_gs 
        || tex_id < 0
        || tex_id >= GFX2D_MAX_TEXTURES 
        || !g_textures[tex_id].used)
        return;

    GSTEXTURE *tex;

    tex = &g_textures[tex_id].tex;

    gsKit_prim_sprite_texture(
        g_gs,
        tex,
        x,     y,
        0.0f,  0.0f,
        x + w, y + h,
        (float)tex->Width, (float)tex->Height,
        1,
        g_modulate
    );
}

void gfx2d_draw_sprite_ex(int tex_id,
                          float x, float y,
                          float w, float h,
                          float origin_x, float origin_y,
                          float scale_x, float scale_y,
                          float rotation_rad,
                          int flip_x, int flip_y)
{
    GSTEXTURE *tex;
    float c, s;
    float sx, sy;
    float lx1, ly1, lx2, ly2, lx3, ly3, lx4, ly4;
    float x1, y1, x2, y2, x3, y3, x4, y4;
    float u_left, u_right, v_top, v_bottom;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    if (w == 0.0f || h == 0.0f)
        return;

    tex = &g_textures[tex_id].tex;

    sx = scale_x;
    sy = scale_y;

    c = cosf(rotation_rad);
    s = sinf(rotation_rad);

    lx1 = -origin_x;     ly1 = -origin_y;
    lx2 = w - origin_x;  ly2 = -origin_y;
    lx3 = -origin_x;     ly3 = h - origin_y;
    lx4 = w - origin_x;  ly4 = h - origin_y;

    lx1 *= sx; ly1 *= sy;
    lx2 *= sx; ly2 *= sy;
    lx3 *= sx; ly3 *= sy;
    lx4 *= sx; ly4 *= sy;

    x1 = x + lx1 * c - ly1 * s;
    y1 = y + lx1 * s + ly1 * c;

    x2 = x + lx2 * c - ly2 * s;
    y2 = y + lx2 * s + ly2 * c;

    x3 = x + lx3 * c - ly3 * s;
    y3 = y + lx3 * s + ly3 * c;

    x4 = x + lx4 * c - ly4 * s;
    y4 = y + lx4 * s + ly4 * c;

    u_left  = flip_x ? (float)tex->Width  : 0.0f;
    u_right = flip_x ? 0.0f               : (float)tex->Width;
    v_top   = flip_y ? (float)tex->Height : 0.0f;
    v_bottom= flip_y ? 0.0f               : (float)tex->Height;

    gfx2d_draw_quad_uv(
        tex_id,
        x1, y1, u_left,  v_top,
        x2, y2, u_right, v_top,
        x3, y3, u_left,  v_bottom,
        x4, y4, u_right, v_bottom
    );
}

void gfx2d_draw_quad(int tex_id,
                     float x1, float y1,
                     float x2, float y2,
                     float x3, float y3,
                     float x4, float y4)
{
    GSTEXTURE *tex;

    if (tex_id < 0 || tex_id >= GFX2D_MAX_TEXTURES || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    gfx2d_draw_quad_uv(
        tex_id,
        x1, y1, 0.0f,               0.0f,
        x2, y2, (float)tex->Width,  0.0f,
        x3, y3, 0.0f,               (float)tex->Height,
        x4, y4, (float)tex->Width,  (float)tex->Height
    );
}