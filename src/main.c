#include <unistd.h>
#include <kernel.h>
#include <debug.h>
#include <graph.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <delaythread.h>
#include <audsrv.h>

#include "logging/log.h"
// #include "mpegv3/mpeg_player.h"
// #include "audio/audio_player.h"
// #include "audio/audio_stream.h"
#include "audio/audio_mixer_stream.h"

#define MPEG_BITSTREAM_FILE "test.bin"
#define MPEG_AUDIO_FILE "audio.raw"
// #define AUDIO_FILE "snd.wav"   // Mamma mia
// #define AUDIO_FILE1 "snd1.wav" // UT_endPartA
// #define AUDIO_FILE2 "snd2.wav" // UT_endPartB

extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;

static int init_audio_system(void)
{
    int irx_ret = SifLoadModule("rom0:LIBSD", 0, NULL);
    LOGLN("[audio] LIBSD load: %d", irx_ret);

    if (irx_ret < 0) {
        LOGLN("[audio] LIBSD load failed: %d — no audio", irx_ret);
        return -1;
    }
    else {
        int mod_ret = 0;
        int mod_id = SifExecModuleBuffer(audsrv_irx, size_audsrv_irx, 0, NULL, &mod_ret);
        LOGLN("[audio] audsrv.irx exec id=%d ret=%d", mod_id, mod_ret);

        if (mod_id < 0 || mod_ret < 0) {
            LOGLN("[audio] audsrv.irx exec failed: id=%d ret=%d — no audio", mod_id, mod_ret);
            return -1;
        } else {
            audsrv_fmt_t fmt = {
                .freq     = 48000,
                .bits     = 16,
                .channels = 2
            };
            if (audsrv_init() != 0) {
                LOGLN("[audio] audsrv_init failed");
                return -1;
            } else if (audsrv_set_format(&fmt) != 0) {
                LOGLN("[audio] audsrv_set_format failed");
                return -1;
            } else {
                audsrv_set_volume(MAX_VOLUME);
                LOGLN("[audio] audsrv ready: 48000 Hz stereo s16");
            }
        }
    }        
    return 0;
}

static void on_stream_started(audio_mixer_t *m, int handle,
                              audio_mix_stream_t *stream, void *userdata)
{
    LOGLN("stream %d started (%s)\n", handle, (const char *)userdata);
}

static void on_stream_stopped(audio_mixer_t *m, int handle,
                              audio_mix_stream_t *stream, void *userdata)
{
    LOGLN("stream %d stopped (%s)\n", handle, (const char *)userdata);
}

int main(void)
{
    SifInitRpc(0);
    SifLoadFileInit();    /* enable SifLoadModule */
    sbv_patch_enable_lmb();
    
    log_init();
    init_scr();
    sleep(3);

    int ret;

    ret = init_audio_system();
    if (ret < 0) {
        LOGLN("audio init failed: %d\n", ret);
        SleepThread();
        return 0;
    }

    const char *mus0 = "1.wav";
    const char *mus1 = "2.wav";
    const char *mus2 = "3.wav";

    audio_mixer_t mixer;
    int music1, music2, music3;

    audio_mixer_init(&mixer, 1024);

    music1 = audio_mixer_add_stream(&mixer, mus0, 128 * 1024);
    //music2 = audio_mixer_add_stream(&mixer, AUDIO_FILE2, 128 * 1024);

    audio_mixer_set_callbacks(&mixer, 
        music1,
        on_stream_started,
        on_stream_stopped,
        mus0);

    audio_mixer_set_volume(&mixer, music1, 100);
    //audio_mixer_set_volume(&mixer, music2, 40);

    audio_mixer_play(&mixer, music1, 1);
    //audio_mixer_play(&mixer, music2, 1);

    DelayThread(20000000);

    music2 = audio_mixer_add_stream(&mixer, mus1, 128 * 1024);
    audio_mixer_set_volume(&mixer, music2, 100);
    audio_mixer_set_callbacks(&mixer, 
        music2,
        on_stream_started,
        on_stream_stopped,
        mus1);

    audio_mixer_play(&mixer, music2, 0);

    DelayThread(2500000);

    music3 = audio_mixer_add_stream(&mixer, mus2, 128 * 1024);
    audio_mixer_set_volume(&mixer, music3, 100);
    audio_mixer_set_callbacks(&mixer, 
        music3,
        on_stream_started,
        on_stream_stopped,
        mus2);

    audio_mixer_play(&mixer, music3, 0);

    DelayThread(214748300);

    audio_mixer_stop(&mixer, music1);
    audio_mixer_stop(&mixer, music2);
    audio_mixer_destroy(&mixer);

    SleepThread();
    return 0;


    //---------------------- audio stream
    // audio_stream_t stream;
    // ret = audio_stream_init(&stream, AUDIO_FILE2, 1024, 64 * 1024);
    // if (ret < 0) {
    //     LOGLN("player init failed: %d\n", ret);
    //     SleepThread();
    //     return 0;
    // }

    // LOGLN("[audio] Set volume: %d", 100);
    // audio_stream_set_volume(&stream, 100);
    // LOGLN("[audio] Set speed: %f", 0.85f);
    // audio_stream_set_speed(&stream, 0.85f);
    // LOGLN("Despite everything, it`s still you.");
    // LOGLN("[audio] Play");
    // audio_stream_play(&stream, 1);
    // DelayThread(214748300);
    // LOGLN("[audio] Stop");
    // audio_stream_stop(&stream);
    // LOGLN("[audio] Destroy");
    // audio_stream_destroy(&stream);


    //---------------------- audio_player

    // int ret;

    // ret = init_audio_system();
    // if (ret < 0) {
    //     LOGLN("audio init failed: %d\n", ret);
    //     SleepThread();
    //     return 0;
    // }

    // audio_player_t player;

    // ret = audio_player_init(&player, AUDIO_FILE1, 1024);
    // if (ret < 0) {
    //     LOGLN("player init failed: %d\n", ret);
    //     SleepThread();
    //     return 0;
    // }

    // LOGLN("[audio] Set volume: %d", 80);
    // audio_player_set_volume(&player, 80);
    // LOGLN("[audio] Set speed: %f", 0.7f);
    // audio_player_set_speed(&player, 0.7f);
    // LOGLN("Despite everything, it`s still you");
    // LOGLN("[audio] Play");
    // audio_player_play(&player, 1);
    // DelayThread(214748300);
    // LOGLN("[audio] Stop");
    // audio_player_stop(&player);
    // LOGLN("[audio] Destroy");
    // audio_player_destroy(&player);

    // LOGLN("[audio] Set volume: %d", 80);
    // audio_player_set_volume(&player, 80);
    // LOGLN("[audio] Set speed: %d", 1);
    // audio_player_set_speed(&player, 1.0f);
    // LOGLN("[audio] Play");
    // audio_player_play(&player, 1);

    // DelayThread(3000000);
    // LOGLN("[audio] Set speed: %f", 0.5f);
    // audio_player_set_speed(&player, 0.5f);

    // DelayThread(3000000);
    // LOGLN("[audio] Pause");
    // audio_player_pause(&player);

    // DelayThread(2000000);
    // LOGLN("[audio] Resume");
    // audio_player_resume(&player);

    // LOGLN("[audio] Set speed: %f", 2.0f);
    // audio_player_set_speed(&player, 2.0f);

    // DelayThread(5000000);
    // LOGLN("[audio] Set speed: %f", 1.25f);
    // audio_player_set_speed(&player, 1.25f);

    // DelayThread(5000000);

    // DelayThread(5000000);
    // LOGLN("[audio] Stop");
    // audio_player_stop(&player);
    // LOGLN("[audio] Destroy");
    // audio_player_destroy(&player);

    // -------------------------------------
}


//     SifInitRpc(0);
//     SifLoadFileInit();    /* enable SifLoadModule */
//     sbv_patch_enable_lmb();

//     log_init();
//     init_scr();
//     sleep(3);

//     LOGLN("Start test...");

//     for (int i = 0; i < 100; ++i) {
//         LOG("Test log %d\n", i);
//         graph_wait_vsync();
//     }

//     LOGLN("Done. Sleeping thread.");
//     SleepThread();
//     return 0;

