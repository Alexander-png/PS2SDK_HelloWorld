#include <unistd.h>
#include <kernel.h>
#include <debug.h>
#include <graph.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <sbv_patches.h>

#include "mpegv3/mpeg_player.h"
#include "logging/log.h"

#define MPEG_BITSTREAM_FILE "test.bin"
#define MPEG_AUDIO_FILE "audio.raw"

int main(void)
{
    SifInitRpc(0);
    SifLoadFileInit();    /* enable SifLoadModule */
    sbv_patch_enable_lmb();

    log_init();
    /* Give scr_printf a moment to be visible if LOG_SCREEN is enabled
     * and we're booting from a real console. */
    sleep(3);

    LOGLN("[main] PS2 MPEG player starting");

    mpeg_player_t player;

    int rc = mpeg_player_init(&player, MPEG_BITSTREAM_FILE, MPEG_AUDIO_FILE);
    if (rc == 0) {
        mpeg_player_run(&player);
        mpeg_player_clear_screen(&player);
        mpeg_player_destroy(&player);
    } else {
        LOGLN("[main] mpeg_player_init failed: %d", rc);
    }
    
    LOGLN("[main] finished");
    SleepThread();
    return 0;

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