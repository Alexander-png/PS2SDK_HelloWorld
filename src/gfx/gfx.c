#include "gfx.h"
#include "../logging/log.h"

#include <kernel.h>
#include <stdlib.h>
#include <string.h>

#include <dma.h>
#include <graph.h>
#include <draw.h>
#include <draw2d.h>
#include <draw_buffers.h>
#include <draw_sampling.h>
#include <draw_primitives.h>
#include <packet2.h>
#include <gs_psm.h>
#include <gif_tags.h>
#include <gs_gp.h>

#define SCREEN_W   640
#define SCREEN_H   240

#define TEX_W      512   /* must be power-of-two on GS */
#define TEX_H      256

static framebuffer_t s_fb;
static zbuffer_t     s_zb;
static texbuffer_t   s_tex;

static packet2_t *s_pkt_setup;
static packet2_t *s_pkt_xfer;
static packet2_t *s_pkt_draw;

void gfx_wait_vsync(void) { graph_wait_vsync(); }

static void setup_display(void) {
    s_fb.width   = SCREEN_W;
    s_fb.height  = SCREEN_H;
    s_fb.psm     = GS_PSM_32;
    s_fb.mask    = 0;
    s_fb.address = graph_vram_allocate(SCREEN_W, SCREEN_H,
                                       GS_PSM_32, GRAPH_ALIGN_PAGE);

    s_zb.enable  = 0;
    s_zb.method  = ZTEST_METHOD_ALLPASS;
    s_zb.zsm     = GS_ZBUF_32;
    s_zb.mask    = 0;
    s_zb.address = graph_vram_allocate(SCREEN_W, SCREEN_H,
                                       GS_ZBUF_32, GRAPH_ALIGN_PAGE);

    s_tex.width   = TEX_W;
    s_tex.psm     = GS_PSM_32;
    s_tex.address = graph_vram_allocate(TEX_W, TEX_H,
                                        GS_PSM_32, GRAPH_ALIGN_BLOCK);
    /* TEX0 info_t: log2 sizes + RGBA components present. */
    s_tex.info.width      = draw_log2(TEX_W);
    s_tex.info.height     = draw_log2(TEX_H);
    s_tex.info.components = TEXTURE_COMPONENTS_RGBA;
    s_tex.info.function   = TEXTURE_FUNCTION_DECAL; /* ignore vertex color */

    graph_initialize(s_fb.address, s_fb.width, s_fb.height,
                     s_fb.psm, 0, 0);
    graph_set_mode(GRAPH_MODE_NONINTERLACED, GRAPH_MODE_NTSC,
                   GRAPH_MODE_FRAME, GRAPH_DISABLE);
    graph_set_screen(0, 0, SCREEN_W, SCREEN_H);
    graph_set_bgcolor(0, 0, 0);
    graph_set_framebuffer_filtered(s_fb.address, s_fb.width,
                                   s_fb.psm, 0, 0);
    graph_enable_output();
}

/* One-time GS state: framebuffer, scissor, xy offset, texture binding,
 * linear filter, clamp wrap. */
static void setup_gs_state(void) {
    s_pkt_setup = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
    qword_t *q = s_pkt_setup->base;

    q = draw_setup_environment(q, 0, &s_fb, &s_zb);
    q = draw_primitive_xyoffset(q, 0,
                                2048 - (SCREEN_W >> 1),
                                2048 - (SCREEN_H >> 1));
    q = draw_texturebuffer(q, 0, &s_tex, NULL);

    /* Linear min/mag, no mipmapping. */
    lod_t lod = {0};
    lod.calculation = LOD_USE_K;
    lod.max_level   = 0;
    lod.mag_filter  = LOD_MAG_LINEAR;
    lod.min_filter  = LOD_MIN_LINEAR;
    lod.l           = 0;
    lod.k           = 0;
    q = draw_texture_sampling(q, 0, &lod);

    texwrap_t wrap = {0};
    wrap.horizontal = WRAP_CLAMP;
    wrap.vertical   = WRAP_CLAMP;
    wrap.minu = 0; wrap.maxu = TEX_W - 1;
    wrap.minv = 0; wrap.maxv = TEX_H - 1;
    q = draw_texture_wrapping(q, 0, &wrap);

    q = draw_finish(q);

    s_pkt_setup->next = q;
    dma_channel_send_packet2(s_pkt_setup, DMA_CHANNEL_GIF, 1);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
}

void gfx_init(void) {
    dma_channel_initialize(DMA_CHANNEL_GIF, NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    setup_display();
    setup_gs_state();

    /* Reused per frame. Sized generously. */
    s_pkt_xfer = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_CHAIN, 0);
    s_pkt_draw = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);

    LOGLN("gfx: %dx%d NTSC, tex %dx%d", SCREEN_W, SCREEN_H, TEX_W, TEX_H);
}

static void upload_texture(const uint8_t *rgba, int w, int h) {
    packet2_reset(s_pkt_xfer, 0);
    qword_t *q = s_pkt_xfer->base;

    /* --- Setup block: DIRECT GIF tag with TRXPOS/TRXREG/BITBLTBUF/TRXDIR --- */
    /* GIF tag: 4 A+D registers, EOP=1 to end this PACKED block */
    DMATAG_CNT(q, 5, 0, 0, 0);                 q++;
    PACK_GIFTAG(q, GIF_SET_TAG(4, 0, 0, 0, GIF_FLG_PACKED, 1),
                GIF_REG_AD);                    q++;

    /* BITBLTBUF: destination */
    q->dw[0] = GS_SET_BITBLTBUF(0, 0, 0,
                                s_tex.address >> 6,  /* DBP, blocks of 64 */
                                TEX_W >> 6,           /* DBW, in /64 units */
                                GS_PSM_32);
    q->dw[1] = GS_REG_BITBLTBUF;                q++;

    /* TRXPOS: dest position (top-left) */
    q->dw[0] = GS_SET_TRXPOS(0, 0, 0, 0, 0);
    q->dw[1] = GS_REG_TRXPOS;                   q++;

    /* TRXREG: transfer dimensions */
    q->dw[0] = GS_SET_TRXREG(w, h);
    q->dw[1] = GS_REG_TRXREG;                   q++;

    /* TRXDIR: kick off HOST→LOCAL */
    q->dw[0] = GS_SET_TRXDIR(0);
    q->dw[1] = GS_REG_TRXDIR;                   q++;

    /* --- Image data block: REF tag pointing to rgba buffer --- */
    /* qwords needed: w*h pixels * 4B / 16B = w*h/4 */
    int data_qwords = (w * h) / 4;
    DMATAG_REF(q, data_qwords, (u32)rgba, 0, 0, 0);   q++;
    PACK_GIFTAG(q, GIF_SET_TAG(data_qwords, 1, 0, 0,
                               GIF_FLG_IMAGE, 0), 0); q++;

    /* --- End tag --- */
    DMATAG_END(q, 0, 0, 0, 0);                  q++;

    s_pkt_xfer->next = q;

    FlushCache(0);
    dma_channel_send_packet2(s_pkt_xfer, DMA_CHANNEL_GIF, 1);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    
    // packet2_reset(s_pkt_xfer, 0);
    // qword_t *q = s_pkt_xfer->base;
    // q = draw_texture_transfer(q, (void*)rgba, w, h,
    //                           GS_PSM_32, s_tex.address, TEX_W);
    // q = draw_texture_flush(q);
    // s_pkt_xfer->next = q;

    // /* explicit flag set so DMAC knows packet length */
    // FlushCache(0);
    // dma_channel_send_packet2(s_pkt_xfer, DMA_CHANNEL_GIF, 1);
    // dma_channel_wait(DMA_CHANNEL_GIF, 0);
}

static void draw_video_sprite(int w, int h) {
    packet2_reset(s_pkt_draw, 0);
    qword_t *q = s_pkt_draw->base;

    texrect_t r;
    r.v0.x = 0;        r.v0.y = 0;        r.v0.z = 0;
    r.v1.x = SCREEN_W; r.v1.y = SCREEN_H; r.v1.z = 0;
    r.t0.u = 0;        r.t0.v = 0;
    r.t1.u = w;        r.t1.v = h;
    r.color.r = 0x80; r.color.g = 0x80;
    r.color.b = 0x80; r.color.a = 0x80;
    r.color.q = 1.0f;

    q = draw_rect_textured(q, 0, &r);
    q = draw_finish(q);

    s_pkt_draw->next = q;
    dma_channel_send_packet2(s_pkt_draw, DMA_CHANNEL_GIF, 1);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    draw_wait_finish();
}

void gfx_present_frame(const uint8_t *rgba, int w, int h) {
    upload_texture(rgba, w, h);
    draw_video_sprite(w, h);
}