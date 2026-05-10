/*
 * mpeg_player.h — PS2 MPEG-1/2 elementary stream player.
 *
 * Loads a raw MPEG video ES from a file, decodes it via the IPU using
 * libmpeg, and displays the result as a textured sprite on the GS.
 *
 * Usage:
 *     mpeg_player_t p;
 *     if (mpeg_player_init(&p, "test.bin") == 0) {
 *         mpeg_player_run(&p);   // blocks until EOF / sequence end
 *     }
 *     mpeg_player_destroy(&p);
 *
 * The player owns the framebuffer/zbuffer setup and the GS draw
 * environment; call it from a context where graphics state is yours
 * to clobber.
 *
 * Resolution support: this is built on the simple ps2dev libmpeg
 * sample and reliably decodes up to ~352x288. 640x480 hangs on the
 * second frame because the underlying example has no reference-frame
 * support and a fixed-size SPR working set. For full-screen video,
 * see the SMS project.
 */

#ifndef MPEG_PLAYER_H
#define MPEG_PLAYER_H

#include <tamtypes.h>
#include <libmpeg.h>
#include <packet.h>
#include <audsrv.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Maximum file size loaded into EE RAM. PS2 has 32 MB total. For
 * 640x480 the decoded picture buffer alone is ~1.2 MB and libmpeg
 * keeps reference frames internally, so leave generous headroom.
 * Streams larger than this are truncated (a warning is logged); for
 * long videos switch to streaming reads inside data_cb instead of
 * preloading. */
#define MPEG_PLAYER_MAX_FILE_SIZE   (16 * 1024 * 1024)

/* DMA chunk size (bytes) sent to the IPU per data callback.
 * Must be a multiple of 16 (qword granularity). */
#define MPEG_PLAYER_DMA_CHUNK       2048

/* Chunk size fed to audsrv per iteration (must divide evenly into
 * stereo s16 samples = multiples of 4). 4 KB is a safe default. */
#define MPEG_AUDIO_CHUNK  2048

typedef struct mpeg_player {
    /* ---- File buffer ---- */
    unsigned char *buffer;       /* malloc'd file contents              */
    unsigned int   buffer_size;  /* bytes actually read into buffer     */
    unsigned char *cursor;       /* read head used by data callback     */

    /* ---- Sequence info captured by init_cb ---- */
    MPEGSequenceInfo info;
    int   info_valid;            /* 1 once init_cb fires                */

    /* ---- Decode-side state (filled in init_cb) ---- */
    void     *picture;           /* RGBA32 macroblock-tiled frame buf   */
    packet_t *xfer_pck;          /* GIF chain: upload picture -> VRAM   */
    packet_t *draw_pck;          /* GIF normal: draw textured sprite    */
    int       tex_addr;          /* VRAM word address of texture        */

    /* ---- libmpeg state ---- */
    s64 pts;                     /* timestamp scratch for libmpeg       */

    /* ---- Diagnostics ---- */
    unsigned int dma_calls;      /* counted in data_cb, logged per frame*/
    unsigned int frames_drawn;
    int eof;   /* set to 1 as soon as data_cb returns 0 */

    /* ---- Audio ---- */
    unsigned char *audio_buffer;     /* malloc'd raw PCM file contents  */
    unsigned int   audio_size;       /* total bytes                     */
    unsigned int   audio_pos;        /* read head (bytes consumed)      */
    int            audio_thread_id;  /* EE thread feeding audsrv        */
    int            audio_running;    /* set to 0 to stop the thread     */
} mpeg_player_t;

/* Load `path` into RAM, validate the bitstream, set up GS, and
 * initialize libmpeg. Returns 0 on success, negative on error. */
int mpeg_player_init(mpeg_player_t *p, const char *video_path, const char *audio_path);

/* Decode and display frames until EOF or sequence end. Blocking. */
void mpeg_player_run(mpeg_player_t *p);

/* Decode + display exactly one frame. Returns 1 on success, 0 on EOF.
 * Useful if you want to interleave with other work. */
int mpeg_player_step(mpeg_player_t *p);

/* Fill the framebuffer with black after playback ends. */
void mpeg_player_clear_screen(mpeg_player_t *p);

/* Tear down libmpeg, free buffers and packets. Safe to call even if
 * mpeg_player_init failed partway through. */
void mpeg_player_destroy(mpeg_player_t *p);

#ifdef __cplusplus
}
#endif

#endif /* MPEG_PLAYER_H */