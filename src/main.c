#include <unistd.h>
#include <kernel.h>
#include <debug.h>
#include <graph.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>
#include <delaythread.h>

#include "mpegv3/mpeg_player.h"
#include "logging/log.h"
//#include "audio/audio_player.h"
#include "audio/audio_stream.h"

#define MPEG_BITSTREAM_FILE "test.bin"
#define MPEG_AUDIO_FILE "audio.raw"
#define AUDIO_FILE "snd.wav"
#define AUDIO_FILE1 "snd1.wav"
#define AUDIO_FILE2 "snd2.wav"

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


int main(void)
{
    SifInitRpc(0);
    SifLoadFileInit();    /* enable SifLoadModule */
    sbv_patch_enable_lmb();
    
    log_init();
    init_scr();
    sleep(3);

    audio_stream_t stream;
    int ret;

    ret = init_audio_system();
    if (ret < 0) {
        LOGLN("audio init failed: %d\n", ret);
        SleepThread();
        return 0;
    }

    ret = audio_stream_init(&stream, AUDIO_FILE2, 1024, 64 * 1024);
    if (ret < 0) {
        LOGLN("player init failed: %d\n", ret);
        SleepThread();
        return 0;
    }

    LOGLN("[audio] Set volume: %d", 80);
    audio_stream_set_volume(&stream, 80);
    LOGLN("[audio] Set speed: %f", 0.7f);
    audio_stream_set_speed(&stream, 0.7f);
    LOGLN("Despite everything, it`s still you");
    LOGLN("[audio] Play");
    audio_stream_play(&stream, 1);
    DelayThread(214748300);
    LOGLN("[audio] Stop");
    audio_stream_stop(&stream);
    LOGLN("[audio] Destroy");
    audio_stream_destroy(&stream);

    // audio_player_t player;
    // int ret;

    // ret = init_audio_system();
    // if (ret < 0) {
    //     LOGLN("audio init failed: %d\n", ret);
    //     SleepThread();
    //     return 0;
    // }

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

    SleepThread();
    return 0;

    // SifInitRpc(0);
    // SifLoadFileInit();    /* enable SifLoadModule */
    // sbv_patch_enable_lmb();

    // log_init();
    // init_scr();
    // sleep(3);

    // LOGLN("Start test...");

    // for (int i = 0; i < 100; ++i) {
    //     LOG("Test log %d\n", i);
    //     graph_wait_vsync();
    // }

    // LOGLN("Done. Sleeping thread.");
    // SleepThread();
    // return 0;
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


    // SifInitRpc(0);
    // SifLoadFileInit();    /* enable SifLoadModule */
    // sbv_patch_enable_lmb();

    // log_init();
    // /* Give scr_printf a moment to be visible if LOG_SCREEN is enabled
    //  * and we're booting from a real console. */
    // sleep(3);

    // LOGLN("[main] PS2 MPEG player starting");

    // mpeg_player_t player;

    // int rc = mpeg_player_init(&player, MPEG_BITSTREAM_FILE, MPEG_AUDIO_FILE);
    // if (rc == 0) {
    //     mpeg_player_run(&player);
    //     mpeg_player_clear_screen(&player);
    //     mpeg_player_destroy(&player);
    // } else {
    //     LOGLN("[main] mpeg_player_init failed: %d", rc);
    // }
    
    // LOGLN("[main] finished");
    // SleepThread();
    // return 0;
