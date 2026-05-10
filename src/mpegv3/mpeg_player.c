/*
 * mpeg_player.c — raw MPEG-1/2 ES player using libmpeg + GS.
 *
 * Pipeline:
 *   file -> RAM buffer -> [IPU via DMA toIPU] -> libmpeg decoder
 *        -> RGBA32 macroblock tiles in EE RAM
 *        -> [GIF chain DMA] -> GS texture VRAM
 *        -> [GIF normal DMA] -> textured sprite on screen
 *
 * Original example by Eugene Plotnikov (ps2dev, 2006-2007), reorganized
 * into a self-contained module with validation and structured logging.
 *
 * --- KNOWN LIMITATIONS ----------------------------------------------
 * This is the simple ps2dev libmpeg sample. It works well for small
 * resolutions (up to ~352x288) but cannot decode 640x480 or larger:
 *   - it provides exactly one picture buffer (no reference frames),
 *     so P/B-frames have nothing to reference;
 *   - libmpeg's internal SPR working set (0x0000… 0x3C00, 15 KB) is
 *     sized for small macroblock counts.
 * If you need full-screen video on PS2, use the SMS project's player
 * (see ps2sdk's libmpeg comment) or a gsKit-based IPU wrapper.
 * Recommended source resolutions for this player: 320x240, 352x240,
 * 352x288, 480x272.
 * --------------------------------------------------------------------
 */

#include "mpeg_player.h"
#include "log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <malloc.h>

#include <kernel.h>
#include <dma.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <gs_gp.h>
#include <draw.h>
#include <graph.h>

#include <loadfile.h>
#include <sbv_patches.h>

/* ------------------------------------------------------------------ */
/* DMAC register access                                                */
/* ------------------------------------------------------------------ */

/* DMAC status register. Bit 8 (CIS8) latches completion of channel 8
 * (toIPU). The original ps2dev libmpeg sample uses this as a "prev
 * transfer settled, OK to push more" gate, and it works in practice.
 * Don't replace with Dn_CHCR.STR polling — ps2sdk's send_normal
 * already waits for STR to clear, so that check is always false and
 * we'd flood the IPU with back-to-back bursts. */
#define D_STAT ((volatile unsigned int *)0x1000E010)
#define D_STAT_CIS8 (1u << 8)

/* ------------------------------------------------------------------ */
/* Framebuffer (module-local; the player owns the screen while it     */
/* runs)                                                              */
/* ------------------------------------------------------------------ */

#define SCREEN_W 640
#define SCREEN_H 480

static framebuffer_t s_frame;
static zbuffer_t     s_z;

/* ------------------------------------------------------------------ */
/* Audio stack                                                        */
/* ------------------------------------------------------------------ */

#define AUDIO_STACK_SIZE 8192
static unsigned char s_audio_stack[AUDIO_STACK_SIZE] __attribute__((aligned(16)));

extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;

// Module-level: store the ID as a plain int, not ee_sema_t
static int s_delay_sema_id = -1;

// In delay_alarm_cb and delay_ms:
static void delay_alarm_cb(s32 alarm_id, u16 time, void *arg) {
    (void)alarm_id; (void)time;
    iSignalSema((int)(uintptr_t)arg);
}

static void delay_ms(int ms) {
    SetAlarm(ms * 60, delay_alarm_cb, (void *)(uintptr_t)s_delay_sema_id);
    WaitSema(s_delay_sema_id);
}

/* ------------------------------------------------------------------ */
/* libmpeg callbacks                                                  */
/* ------------------------------------------------------------------ */

/*
 * Data callback. libmpeg invokes this to push more bitstream bytes
 * into the IPU's FIFO. The original example's signature is "feed a
 * DMA burst, return non-zero to keep going, zero on EOF".
 */
static int data_cb(void *userdata)
{
    mpeg_player_t *p = (mpeg_player_t *)userdata;

    p->dma_calls++;

    /* Periodic heartbeat so we can tell from logs whether data_cb is
     * being called at all and how the cursor is advancing. Logs every
     * 256th call — enough to see motion without flooding. */
    DEBUG_ONLY(
        if ((p->dma_calls & 0xFF) == 1) {
            unsigned int pos = (unsigned int)(p->cursor - p->buffer);
            LOGLN("[mpeg] data_cb #%u: cursor=%u/%u D_STAT=%08x",
                  p->dma_calls, pos, p->buffer_size, *D_STAT);
        }
    );

    /* After (logs exactly once): */
    if (p->cursor >= p->buffer + p->buffer_size) {
        if (!p->eof) {
            LOGLN("[mpeg] data_cb: EOF at #%u", p->dma_calls);
        }
        p->eof = 1;   // <-- new
        return 0;
    }

    /* If the previous toIPU burst is still in flight, just tell
     * libmpeg "alive, come back later" — same as the original sample. */
    if (*D_STAT & D_STAT_CIS8) {
        return 1;
    }

    int remaining = (int)((p->buffer + p->buffer_size) - p->cursor);
    int size      = (remaining > MPEG_PLAYER_DMA_CHUNK)
                        ? MPEG_PLAYER_DMA_CHUNK
                        : remaining;

    /* qwc in 16-byte units, matching the original ps2dev sample. */
    dma_channel_send_normal(DMA_CHANNEL_toIPU, p->cursor, size >> 4, 0, 0);
    p->cursor += size;

    return 1;
}

/*
 * Init callback. Called by libmpeg once it has parsed the sequence
 * header and knows width/height. We allocate the RGBA32 picture buffer
 * and build the two GIF packets (upload + draw).
 */
static void *init_cb(void *userdata, MPEGSequenceInfo *si)
{
    mpeg_player_t *p = (mpeg_player_t *)userdata;

    p->info       = *si;
    p->info_valid = 1;

    LOGLN("[mpeg] sequence header parsed:");
    LOGLN("       size       = %dx%d",  si->m_Width, si->m_Height);
    LOGLN("       chroma_fmt = %d (1=4:2:0, 2=4:2:2, 3=4:4:4)",
          si->m_ChromaFmt);
    LOGLN("       video_fmt  = %d (1=PAL, 2=NTSC)", si->m_VideoFmt);
    LOGLN("       ms/frame   = %d", si->m_MSPerFrame);

    if (si->m_ChromaFmt != MPEG_CHROMA_FORMAT_420) {
        LOGLN("[mpeg] WARNING: only 4:2:0 supported, got %d",
              si->m_ChromaFmt);
    }
    if ((si->m_Width & 0xF) || (si->m_Height & 0xF)) {
        LOGLN("[mpeg] WARNING: dimensions not multiple of 16 (%dx%d)",
              si->m_Width, si->m_Height);
    }

    /* ---- Allocate decoded-picture buffer (RGBA32, MB-tiled) ---- */
    int data_size = si->m_Width * si->m_Height * 4;
    char *pic     = (char *)memalign(64, data_size);

    if (!pic) {
        LOGLN("[mpeg] FATAL: memalign(%d) failed", data_size);
        return NULL;
    }
    
    SyncDCache(pic, pic + data_size);
    p->picture = pic;

    /* ---- Geometry helpers ---- */
    int mbw = si->m_Width  >> 4;          /* macroblocks across   */
    int mbh = si->m_Height >> 4;          /* macroblocks down     */
    int tbw = (si->m_Width + 63) >> 6;    /* texture buffer width */
    int tw  = draw_log2(si->m_Width);
    int th  = draw_log2(si->m_Height);

    /* tex_addr was passed in by mpeg_player_init in word units;
     * GS_SET_BITBLTBUF / GS_SET_TEX0 want it in 64-byte page units. */
    int tex_addr_pages = p->tex_addr >> 6;

    /* ---- Upload packet: chain of DMA tags, one block per MB ----
     * Header: 4 qwords (DMATAG_CNT + GIFTAG_AD + TRXREG + BITBLTBUF).
     * Per macroblock: 6 qwords (DMATAG_CNT + GIFTAG_AD +
     *                            TRXPOS + TRXDIR +
     *                            GIFTAG_image + DMATAG_REF).
     * Add a small safety margin. The original sample's formula
     * (10 + 12*mbw*mbh)>>1 is *just* enough for small videos but
     * unnervingly close at 640x480 — give it real headroom. */
    int xfer_qwc = 4 + 6 * mbw * mbh + 8;
    p->xfer_pck  = packet_init(xfer_qwc, PACKET_NORMAL);

    qword_t *q = p->xfer_pck->data;

    DMATAG_CNT(q, 3, 0, 0, 0); q++;
    PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TRXREG(16, 16), GS_REG_TRXREG); q++;
    PACK_GIFTAG(q, GS_SET_BITBLTBUF(0, 0, 0,
                                    tex_addr_pages, tbw, GS_PSM_32),
                GS_REG_BITBLTBUF); q++;

    char *img = pic;
    int   lx, ly;
    for (ly = 0; ly < si->m_Height; ly += 16) {
        for (lx = 0; lx < si->m_Width; lx += 16, img += 1024) {
            DMATAG_CNT(q, 4, 0, 0, 0); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(2, 0, 0, 0, 0, 1),
                        GIF_REG_AD); q++;
            PACK_GIFTAG(q, GS_SET_TRXPOS(0, 0, lx, ly, 0),
                        GS_REG_TRXPOS); q++;
            PACK_GIFTAG(q, GS_SET_TRXDIR(0), GS_REG_TRXDIR); q++;
            PACK_GIFTAG(q, GIF_SET_TAG(64, 1, 0, 0, 2, 0), 0); q++;
            DMATAG_REF(q, 64, (unsigned)img, 0, 0, 0); q++;
        }
    }
    /* No DMATAG_END here — send_chain stops on the explicit qwc. */

    p->xfer_pck->qwc = q - p->xfer_pck->data;

    /* ---- Draw packet: textured sprite covering the framebuffer ----
     * GS coords are 12.4 fixed point with the screen origin at (2048,
     * 2048). We use 640x512 (rather than 480) to fully cover PAL
     * overscan; on NTSC consoles the picture will be slightly
     * vertically stretched. */
    p->draw_pck = packet_init(7, PACKET_NORMAL);
    q = p->draw_pck->data;

    PACK_GIFTAG(q, GIF_SET_TAG(6, 1, 0, 0, 0, 1), GIF_REG_AD); q++;
    PACK_GIFTAG(q, GS_SET_TEX0(tex_addr_pages, tbw, GS_PSM_32,
                               tw, th, 1, 1, 0, 0, 0, 0, 0),
                GS_REG_TEX0_1); q++;
    PACK_GIFTAG(q, GS_SET_PRIM(6, 0, 1, 0, 0, 0, 1, 0, 0),
                GS_REG_PRIM); q++;
    PACK_GIFTAG(q, GS_SET_UV(0, 0), GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((2048 << 4), (2048 << 4), 0),
                GS_REG_XYZ2); q++;
    PACK_GIFTAG(q, GS_SET_UV(si->m_Width << 4, si->m_Height << 4),
                GS_REG_UV); q++;
    PACK_GIFTAG(q, GS_SET_XYZ((SCREEN_W << 4) + (2048 << 4),
                              (512      << 4) + (2048 << 4), 0),
                GS_REG_XYZ2); q++;

    p->draw_pck->qwc = q - p->draw_pck->data;

    LOGLN("[mpeg] init_cb: picture=%p tex_addr=%d xfer_qwc=%u draw_qwc=%u",
          p->picture, p->tex_addr,
          p->xfer_pck->qwc, p->draw_pck->qwc);

    /* The pointer we return is what libmpeg will write decoded
     * picture data into. */
    return pic;
}

/* ------------------------------------------------------------------ */
/* File loading + bitstream sanity check                              */
/* ------------------------------------------------------------------ */

static int load_file(const char *path,
                     unsigned char **out_buf,
                     unsigned int *out_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        LOGLN("[mpeg] open(%s) failed: %d", path, fd);
        return -1;
    }

    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if (size <= 0) {
        LOGLN("[mpeg] lseek returned %d for %s", size, path);
        close(fd);
        return -2;
    }

    int read_size = size;
    if (read_size > MPEG_PLAYER_MAX_FILE_SIZE) {
        LOGLN("[mpeg] WARNING: %s is %d bytes, capping at %d",
              path, size, MPEG_PLAYER_MAX_FILE_SIZE);
        read_size = MPEG_PLAYER_MAX_FILE_SIZE;
    }

    /* memalign(64, ...) so the buffer is safe for SyncDCache and
     * the IPU DMA which expects 16-byte alignment at minimum. */
    unsigned char *buf = (unsigned char *)memalign(64, read_size);
    if (!buf) {
        LOGLN("[mpeg] memalign(%d) failed", read_size);
        close(fd);
        return -3;
    }

    int n = read(fd, buf, read_size);
    close(fd);

    if (n != read_size) {
        LOGLN("[mpeg] short read: requested %d, got %d", read_size, n);
        free(buf);
        return -4;
    }

    /* IPU reads from main RAM directly: make sure cached writes from
     * the EE during read() are visible. */
    SyncDCache(buf, buf + read_size);

    *out_buf  = buf;
    *out_size = (unsigned int)read_size;
    LOGLN("[mpeg] loaded %s: %u bytes", path, *out_size);
    return 0;
}

static int validate_bitstream(const mpeg_player_t *p, const char *path)
{
    /* Raw MPEG video ES must start with a sequence header start code
     * 0x00 0x00 0x01 0xB3. Anything else (PS / TS / VOB / WAV / ...)
     * means the user forgot to demux or extract the ES. */
    if (p->buffer_size < 4 ||
        p->buffer[0] != 0x00 || p->buffer[1] != 0x00 ||
        p->buffer[2] != 0x01 || p->buffer[3] != 0xB3)
    {
        LOGLN("[mpeg] ERROR: %s does not start with MPEG sequence header.",
              path);
        if (p->buffer_size >= 8) {
            LOGLN("       First 8 bytes: %02x %02x %02x %02x %02x %02x %02x %02x",
                  p->buffer[0], p->buffer[1], p->buffer[2], p->buffer[3],
                  p->buffer[4], p->buffer[5], p->buffer[6], p->buffer[7]);
        }
        LOGLN("       Expected:      00 00 01 b3 ...");
        LOGLN("       Hint: extract a raw video ES with:");
        LOGLN("       ffmpeg -i src.mpg -an -c:v copy -f mpeg2video test.bin");
        return -11;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* GS / DMA setup                                                     */
/* ------------------------------------------------------------------ */

static void setup_graphics(mpeg_player_t *p)
{
    s_frame.width   = SCREEN_W;
    s_frame.height  = SCREEN_H;
    s_frame.mask    = 0;
    s_frame.psm     = GS_PSM_32;
    s_frame.address = graph_vram_allocate(s_frame.width, s_frame.height,
                                          s_frame.psm, GRAPH_ALIGN_PAGE);

    s_z.enable  = 0;
    s_z.mask    = 0;
    s_z.method  = 0;
    s_z.zsm     = 0;
    s_z.address = 0;

    dma_channel_initialize(DMA_CHANNEL_toIPU, NULL, 0);
    dma_channel_initialize(DMA_CHANNEL_GIF,   NULL, 0);
    dma_channel_fast_waits(DMA_CHANNEL_GIF);

    graph_initialize(0, SCREEN_W, SCREEN_H, GS_PSM_32, 0, 0);

    /* Texture VRAM: place it right after the framebuffer. The init
     * callback converts this word address to 64-byte pages. */
    p->tex_addr = graph_vram_allocate(0, 0, GS_PSM_32, GRAPH_ALIGN_BLOCK);

    /* Set up draw environment + clear screen once. */
    packet_t *pck = packet_init(100, PACKET_NORMAL);
    qword_t  *q   = pck->data;

    q = draw_setup_environment(q, 0, &s_frame, &s_z);
    q = draw_clear(q, 0, 0, 0,
                   (float)SCREEN_W, (float)SCREEN_H,
                   0, 0, 0);

    dma_channel_send_normal(DMA_CHANNEL_GIF, pck->data,
                            q - pck->data, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet_free(pck);
}

/* ---- Load audio ---- */
static int load_audio(mpeg_player_t *p, const char *audio_path)
{
    if (!p || !audio_path || !audio_path[0]) {
        return -1;
    }

    int fd = open(audio_path, O_RDONLY);
    if (fd < 0) {
        LOGLN("[audio] open(%s) failed — no audio", audio_path);
        return -1;
    }

    int size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);

    if (size <= 0) {
        close(fd);
        LOGLN("[audio] invalid audio size: %d", size);
        return -1;
    }

    p->audio_buffer = (unsigned char *)memalign(64, size);
    if (!p->audio_buffer) {
        close(fd);
        LOGLN("[audio] memalign(%d) failed", size);
        return -1;
    }

    int n = read(fd, p->audio_buffer, size);
    close(fd);

    if (n != size) {
        LOGLN("[audio] short read: requested %d, got %d", size, n);
        free(p->audio_buffer);
        p->audio_buffer = NULL;
        return -1;
    }

    SyncDCache(p->audio_buffer, p->audio_buffer + size);
    p->audio_size = size;
    p->audio_pos  = 0;
    LOGLN("[audio] loaded %s: %u bytes", audio_path, p->audio_size);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Audio handling                                                     */
/* ------------------------------------------------------------------ */

static void audio_thread(void *arg)
{
    mpeg_player_t *p = (mpeg_player_t *)arg;

    while (p->audio_running) {
        if (p->audio_pos >= p->audio_size) break;

        int remaining = (int)(p->audio_size - p->audio_pos);
        int chunk = (remaining > MPEG_AUDIO_CHUNK) ? MPEG_AUDIO_CHUNK : remaining;

        audsrv_wait_audio(chunk);
        audsrv_play_audio((char *)p->audio_buffer + p->audio_pos, chunk);
        p->audio_pos += chunk;
    }

    p->audio_running = 0;
    ExitDeleteThread();
}

static void start_audio_thread(mpeg_player_t *p)
{
    if (!p->audio_buffer) return;

    p->audio_running = 1;

    ee_thread_t t = {
        .func           = audio_thread,
        .stack          = s_audio_stack,
        .stack_size     = AUDIO_STACK_SIZE,
        .gp_reg         = &_gp,
        .initial_priority = 16,   /* lower than video (default ~32) */
    };

    p->audio_thread_id = CreateThread(&t);
    if (p->audio_thread_id >= 0) {
        StartThread(p->audio_thread_id, p);
        LOGLN("[audio] feeder thread started (id=%d)", p->audio_thread_id);
    } else {
        LOGLN("[audio] CreateThread failed: %d", p->audio_thread_id);
        p->audio_running = 0;
    }
}

static void stop_audio_thread(mpeg_player_t *p)
{
    if (p->audio_thread_id < 0) return;
    //p->audio_running = 0;
    /* Give the thread a moment to exit cleanly. */
    int tries = 50;
    p->audio_running = 0;
    // Wait for the thread to clear the flag itself via ExitDeleteThread
    while (tries-- > 0 && p->audio_thread_id >= 0) {
        delay_ms(2);
    }

    p->audio_thread_id = -1;
}

/* ------------------------------------------------------------------ */
/* Public API                                                         */
/* ------------------------------------------------------------------ */

int mpeg_player_init(mpeg_player_t *p,
                     const char *video_path,
                     const char *audio_path)
{
    if (!p || !video_path) return -1;

    /* ---- IOP setup (must happen once, before any IOP RPC calls) ---- */
    /* Allow loading modules from host/mc. Only needed if not already
    * done in main(). Safe to call multiple times. */
    sbv_patch_enable_lmb();   /* safe to call multiple times */

    memset(p, 0, sizeof(*p));

    ee_sema_t sem = { .init_count = 0, .max_count = 1, .option = 0 };
    s_delay_sema_id = CreateSema(&sem);

    p->audio_thread_id = -1;

    /* ---- Load + validate video ---- */
    int rc = load_file(video_path, &p->buffer, &p->buffer_size);
    if (rc != 0) return rc;


    p->cursor = p->buffer;
    p->pts    = 0;

    rc = validate_bitstream(p, video_path);
    if (rc != 0) {
        mpeg_player_destroy(p);
        return rc;
    }

    setup_graphics(p);

    /* ---- Load audio file (optional) ---- */
    load_audio(p, audio_path);   /* fills p->audio_buffer/size/pos or leaves NULL */

    if (p->audio_buffer) {
        int irx_ret = SifLoadModule("rom0:LIBSD", 0, NULL);
        LOGLN("[audio] LIBSD load: %d", irx_ret);

        if (irx_ret < 0) {
            LOGLN("[audio] LIBSD load failed: %d — no audio", irx_ret);
        } else {
            int mod_ret = 0;
            int mod_id = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &mod_ret);
            LOGLN("[audio] audsrv.irx exec id=%d ret=%d", mod_id, mod_ret);

            if (mod_id < 0 || mod_ret < 0) {
                LOGLN("[audio] audsrv.irx exec failed: id=%d ret=%d — no audio", mod_id, mod_ret);
            } else {
                audsrv_fmt_t fmt = {
                    .freq     = 48000,
                    .bits     = 16,
                    .channels = 2
                };
                if (audsrv_init() != 0) {
                    LOGLN("[audio] audsrv_init failed");
                } else if (audsrv_set_format(&fmt) != 0) {
                    LOGLN("[audio] audsrv_set_format failed");
                } else {
                    audsrv_set_volume(MAX_VOLUME);
                    LOGLN("[audio] audsrv ready: 48000 Hz stereo s16");
                }
            }
        }
    }

    LOGLN("[mpeg] calling MPEG_Initialize...");
    MPEG_Initialize(data_cb, p, init_cb, p, &p->pts);
    LOGLN("[mpeg] MPEG_Initialize returned");

    return 0;
}

// Ok, now there is the task. I have an .ogg file. I need to:
// 1. Convert it to required format
// 2. Read the file
// 3. Implement playback. It must support non blocking start, pause, loop, change volume and playback speed.

int mpeg_player_step(mpeg_player_t *p)
{
    if (!p) return -1;

    /* If data_cb already signalled EOF, don't attempt another decode. */
    if (p->eof) {
        LOGLN("[mpeg] step: eof flag set, bailing out");
        return 0;
    }

    /* Invalidate EE D-cache for the picture buffer before decoding.
     * IPU writes decoded macroblocks via DMA, bypassing the cache;
     * stale cache lines from a previous frame would otherwise survive
     * and confuse subsequent reads. */
    if (p->info_valid && p->picture) {
        int sz = p->info.m_Width * p->info.m_Height * 4;
        SyncDCache(p->picture, (char *)p->picture + sz);
    }

    /* Decode one picture into p->picture. Returns nonzero on success,
     * 0 on EOF or sequence end. */
    if (!MPEG_Picture(p->picture, &p->pts)) {
        return 0;
    }

    DEBUG_ONLY(
        if (p->frames_drawn < 5) {
            LOGLN("[mpeg] frame %u: pts=%lld ms_per_frame=%d",
                  p->frames_drawn, (long long)p->pts, p->info.m_MSPerFrame);
        }
    );

    /* A/V sync: if video is more than 80ms ahead of the audio feeder,
     * stall here until audio catches up. audio_pos advances in the
     * audio thread independently via audsrv_wait_audio backpressure. */
    if (p->audio_buffer && p->info_valid && p->audio_running) {
        const int bytes_per_sec = 48000 * 2 * 2;
        int video_pts_ms = (int)(p->pts);
        int audio_pts_ms = (int)((long long)p->audio_pos * 1000 / bytes_per_sec);

        while (p->audio_running && (video_pts_ms - audio_pts_ms) > 80) {
            delay_ms(5);
            audio_pts_ms = (int)((long long)p->audio_pos * 1000 / bytes_per_sec);
        }
    }

    /* Make sure any previous GIF transfer is done before we kick a
     * new chain. */
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    dma_wait_fast();

    /* Upload decoded macroblocks -> GS texture VRAM. */
    dma_channel_send_chain(DMA_CHANNEL_GIF,
                           p->xfer_pck->data, p->xfer_pck->qwc,
                           0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);

    /* Pace video against audio clock if audio is running, otherwise
     * against the sequence-header frame period using a wall-clock 
     * accumulator. This avoids the 2-vsync rounding error and lets 
     * the audio thread breathe between frames. */
    if (p->audio_buffer && p->audio_running) {
        /* Audio-master sync: video waits for audio_pos to catch up
         * to the frame we're about to display. */
        const int bytes_per_sec = 48000 * 2 * 2;  /* 192000 */
        unsigned int target_byte = 
            (unsigned int)((long long)p->frames_drawn 
            * p->info.m_MSPerFrame 
            * bytes_per_sec / 1000);
    
        /* Wait until audio has consumed up to this frame's timestamp.
        * delay_ms(1) yields to the audio thread. */
        int guard = 200;  /* max 200ms wait, prevents deadlock if audio dies */
        while (p->audio_running 
            && p->audio_pos < target_byte 
            && guard-- > 0) {
            delay_ms(1);
        }
    } else {
        /* No audio: pace against wall-clock using libc clock(). 
        * CLOCKS_PER_SEC on ps2sdk is 147456 (EE bus clock / 256). */
        #include <time.h>
        static clock_t next_frame_clk = 0;
        clock_t now = clock();
    
        if (next_frame_clk == 0) next_frame_clk = now;
        next_frame_clk += (clock_t)((long long)p->info.m_MSPerFrame 
                                * CLOCKS_PER_SEC / 1000);
    
        /* If we're behind, skip the wait entirely (drop to next frame). */
        while (clock() < next_frame_clk) {
            graph_wait_vsync();
        }
    }

    /* Draw the textured sprite. */
    dma_channel_send_normal(DMA_CHANNEL_GIF,
                            p->draw_pck->data, p->draw_pck->qwc,
                            0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);

    p->frames_drawn++;
    return 1;
}

void mpeg_player_clear_screen(mpeg_player_t *p)
{
    (void)p; /* framebuffer is module-local (s_frame/s_z) */

    packet_t *pck = packet_init(20, PACKET_NORMAL);
    qword_t  *q   = pck->data;

    q = draw_setup_environment(q, 0, &s_frame, &s_z);
    q = draw_clear(q, 0, 0, 0,
                   (float)SCREEN_W, (float)SCREEN_H,
                   0, 0, 0);

    dma_channel_send_normal(DMA_CHANNEL_GIF, pck->data,
                            q - pck->data, 0, 0);
    dma_channel_wait(DMA_CHANNEL_GIF, 0);
    packet_free(pck);

    LOGLN("[mpeg] screen cleared");
}

void mpeg_player_run(mpeg_player_t *p)
{
    if (!p) return;

    LOGLN("[mpeg] entering decode loop");

    start_audio_thread(p);

    for (;;) {
        if (!mpeg_player_step(p)) {
            LOGLN("[mpeg] decode loop done: frames=%u dma_calls=%u eof=%d",
                  p->frames_drawn, p->dma_calls,
                  p->info_valid ? p->info.m_fEOF : -1);
            break;
        }
    }

    stop_audio_thread(p); 
}

void mpeg_player_destroy(mpeg_player_t *p)
{
    if (!p) return;

    stop_audio_thread(p);

    /* Order matters: stop the decoder before freeing the picture
     * buffer it might still reference. */
    MPEG_Destroy();

    if (p->audio_buffer) {
        audsrv_quit();
        free(p->audio_buffer);
        p->audio_buffer = NULL;
    }

    if (p->xfer_pck) { packet_free(p->xfer_pck); p->xfer_pck = NULL; }
    if (p->draw_pck) { packet_free(p->draw_pck); p->draw_pck = NULL; }

    if (p->picture) { free(p->picture); p->picture = NULL; }
    if (p->buffer)  { free(p->buffer);  p->buffer  = NULL; }

    p->buffer_size  = 0;
    p->cursor       = NULL;
    p->info_valid   = 0;
}