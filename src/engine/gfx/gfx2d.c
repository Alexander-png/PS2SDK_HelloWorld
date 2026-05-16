/* engine/gfx/gfx2d.c */
#include "engine/logging/log.h"
#include "gfx2d.h"

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>
#include <string.h>

typedef struct texture_slot {
    int used;
    GSTEXTURE tex;
} texture_slot_t;

static GSGLOBAL *g_gs;
static texture_slot_t g_textures[16];

static u64 g_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);
static u64 g_modulate    = GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00);

int gfx2d_init(void)
{
    g_gs = gsKit_init_global();
    if (!g_gs)
        return -1;

    g_gs->PSM  = GS_PSM_CT32;
    g_gs->PSMZ = GS_PSMZ_16S;
    g_gs->Mode = gsKit_check_rom();

    if (g_gs->Mode == GS_MODE_PAL)
        g_gs->Height = 512;
    else
        g_gs->Height = 448;

    dmaKit_init(D_CTRL_RELE_OFF,
                D_CTRL_MFD_OFF,
                D_CTRL_STS_UNSPEC,
                D_CTRL_STD_OFF,
                D_CTRL_RCYC_8,
                1 << DMA_CHANNEL_GIF);
    dmaKit_chan_init(DMA_CHANNEL_GIF);

    gsKit_init_screen(g_gs);
    g_gs->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_mode_switch(g_gs, GS_ONESHOT);

    memset(g_textures, 0, sizeof(g_textures));
    return 0;
}

void gfx2d_shutdown(void)
{
}

void gfx2d_begin_frame(void)
{
    gsKit_clear(g_gs, GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00));
}

void gfx2d_end_frame(void)
{
    gsKit_queue_exec(g_gs);
    gsKit_sync_flip(g_gs);
}

int gfx2d_load_texture(const char *path, int *out_tex_id)
{
    int i;

    for (i = 0; i < 16; ++i) {
        if (!g_textures[i].used) {
            memset(&g_textures[i].tex, 0, sizeof(GSTEXTURE));

            if (gsKit_texture_png(g_gs, &g_textures[i].tex, path) < 0)
                return -1;

            g_textures[i].used = 1;
            *out_tex_id = i;
            return 0;
        }
    }

    return -1;
}

void gfx2d_draw_sprite(int tex_id, float x, float y, float w, float h)
{
    GSTEXTURE *tex;

    if (tex_id < 0 || tex_id >= 16 || !g_textures[tex_id].used)
        return;

    tex = &g_textures[tex_id].tex;

    gsKit_prim_sprite_texture(
        g_gs,
        tex,
        x,     y,
        0.0f,  0.0f,
        x + w, y + h,
        (float)tex->Width * 2.0, (float)tex->Height * 2.0,
        1,
        GS_SETREG_RGBAQ(0x80, 0x80, 0x80, 0x80, 0x00)
    );
}