#include "engine/gfx/renderer.h"
#include "engine/gfx/texture.h"
#include "engine/gfx/sprite.h"
#include "engine/gfx/internal/renderer_internal.h"
#include "engine/logging/log.h"

#include <gsKit.h>
#include <dmaKit.h>
#include <gsToolkit.h>

static int g_renderer_enabled = 1;
static GSGLOBAL *g_gs = NULL;
static u32 g_frame_index = 0;
static u64 g_clear_color = GS_SETREG_RGBAQ(0x00, 0x00, 0x00, 0x80, 0x00);

static u8 renderer_modulate_to_gs(u8 value)
{
    return (u8)(((int)value * 128 + 127) / 255);
}

static void renderer_reset_runtime_state(void)
{
    g_gs = NULL;
    g_frame_index = 0;
}

GSGLOBAL *renderer_ps2_gs(void)
{
    return g_gs;
}

u32 renderer_frame_index(void)
{
    return g_frame_index;
}

int renderer_init(void)
{
    g_gs = gsKit_init_global();
    if (!g_gs) {
        LOGLNC(LOGCAT_GFX, "[renderer] gsKit_init_global failed");
        return -1;
    }

    g_gs->PSM = GS_PSM_CT32;
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
    gsKit_set_primalpha(g_gs, GS_SETREG_ALPHA(0, 1, 0, 1, 0), 0);
    gsKit_set_test(g_gs, GS_ZTEST_OFF);
    gsKit_mode_switch(g_gs, GS_ONESHOT);

    g_frame_index = 0;

    if (gfx_texture_system_init() != 0) {
        LOGLNC(LOGCAT_GFX, "[renderer] gfx_texture_system_init failed");
        renderer_reset_runtime_state();
        return -1;
    }

    if (gfx_sprite_system_init() != 0) {
        LOGLNC(LOGCAT_GFX, "[renderer] gfx_sprite_system_init failed");
        gfx_texture_system_shutdown();
        renderer_reset_runtime_state();
        return -1;
    }

    LOGLNC(LOGCAT_GFX, "[renderer] init mode=%d height=%d",
           g_gs->Mode,
           g_gs->Height);
    return 0;
}

void renderer_shutdown(void)
{
    if (!g_gs)
        return;

    gfx_sprite_system_shutdown();
    gfx_texture_system_shutdown();

    LOGLNC(LOGCAT_GFX, "[renderer] shutdown frame=%u",
           (unsigned int)g_frame_index);

    renderer_reset_runtime_state();
}

void renderer_set_enabled(int enabled)
{
    g_renderer_enabled = enabled ? 1 : 0;
}

int renderer_is_enabled(void)
{
    return g_renderer_enabled;
}

void renderer_begin_frame(void)
{
    int old_alpha;

    if (!g_gs || !g_renderer_enabled)
        return;

    old_alpha = g_gs->PrimAlphaEnable;
    g_gs->PrimAlphaEnable = GS_SETTING_OFF;
    gsKit_clear(g_gs, g_clear_color);
    g_gs->PrimAlphaEnable = old_alpha;
}

void renderer_end_frame(void)
{
    if (!g_gs || !g_renderer_enabled)
        return;

    gsKit_queue_exec(g_gs);
    gsKit_sync_flip(g_gs);
    gsKit_TexManager_nextFrame(g_gs);
    ++g_frame_index;
}

void renderer_set_clear_color_rgba(unsigned char r,
                                   unsigned char g,
                                   unsigned char b,
                                   unsigned char a)
{
    g_clear_color = GS_SETREG_RGBAQ(
        renderer_modulate_to_gs(r),
        renderer_modulate_to_gs(g),
        renderer_modulate_to_gs(b),
        renderer_modulate_to_gs(a),
        0x00
    );
}

int renderer_get_stats(gfx_renderer_stats_t *out)
{
    if (!out)
        return -1;

    out->frame_index = g_frame_index;
    out->enabled = g_renderer_enabled;
    out->mode = -1;
    out->width = 0;
    out->height = 0;

    if (!g_gs)
        return 0;

    out->mode = g_gs->Mode;
    out->width = g_gs->Width;
    out->height = g_gs->Height;
    return 0;
}

int renderer_get_screen_width() {
    if (!g_gs)
        return 0;
    return g_gs->Width;
}
int renderer_get_screen_height(){
    if (!g_gs)
        return 0;
    return g_gs->Height;
}