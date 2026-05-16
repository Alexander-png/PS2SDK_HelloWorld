#include "boot.h"
#include "engine/logging/log.h"
#include "engine/platform/platform.h"
#include "engine/audio/audio.h"
#include "engine/input/input.h"
#include "engine/streaming/streaming.h"
#include "engine/resources/resources.h"
#include "engine/gfx/gfx2d.h"

#include <string.h>

#if defined(LOG_SCREEN)
#include <debug.h>
#endif

static int boot_init_screen(boot_context_t *boot)
{
#if defined(LOG_SCREEN)
    init_scr();
    log_enable_screen(1);
    boot->screen_available = 1;
    LOGLN("[boot] screen ready");
#else
    boot->screen_available = 0;
#endif

    return 0;
}

static void boot_shutdown_screen(boot_context_t *boot)
{
    if (!boot->screen_available)
        return;

    log_enable_screen(0);
    boot->screen_available = 0;
}

int boot_init(boot_context_t *boot)
{
    if (!boot)
        return -1;

    memset(boot, 0, sizeof(*boot));

    log_init();
    LOGLN("[boot] logger ready");

    if (boot_init_screen(boot) < 0) {
        LOGLN("[boot] screen init failed");
        return -2;
    }

    LOGLN("[boot] starting");

    if (platform_init() < 0) {
        LOGLN("[boot] platform init failed");
        return -3;
    }

    /*
     * Future mandatory systems:
     *
     * memory_init();
     * filesystem_init();
     * streaming_init();
     * resource_manager_init();
     * gfx2d_init();
     */

    if (input_init() < 0) {
        boot->input_available = 0;
        LOGLN("[boot] input init failed, continuing without input");
    } else {
        boot->input_available = input_is_available();
        if (boot->input_available)
            LOGLN("[boot] input available");
        else
            LOGLN("[boot] input unavailable, continuing");
    }

    if (streaming_init() < 0) {
        LOGLN("[boot] streaming init failed");
        return -4;
    }

    if (resources_init() < 0) {
        LOGLN("[boot] resources init failed");
        return -5;
    }

    if (gfx2d_init() < 0) {
        LOGLN("[boot] gfx2d init failed");
        return -6;
    } else {
        // LOGLN uses scr_printf() that uses PS2SDK’s debug screen drawing path, 
        // and that path is not meant to coexist cleanly with gsKit, which
        // gfx2d uses. So disable debug scrren if gfx2d initialized
        log_enable_screen(0);
    }

    if (audio_init() < 0) {
        /*
         * audio_init() currently returns 0 even when audio is disabled,
         * but keep this branch for future stricter behavior.
         */
        boot->audio_available = 0;
        LOGLN("[boot] audio init failed, continuing without audio");
    } else {
        boot->audio_available = audio_is_available();
        if (boot->audio_available)
            LOGLN("[boot] audio available");
        else
            LOGLN("[boot] audio unavailable, continuing");
    }

    boot->initialized = 1;
    LOGLN("[boot] ready");

    return 0;
}

void boot_shutdown(boot_context_t *boot)
{
    if (!boot)
        return;

    if (!boot->initialized) {
        boot_shutdown_screen(boot);
        return;
    }

    LOGLN("[boot] shutdown");

    /*
     * Future shutdown order:
     *
     * gfx2d_shutdown();
     * resource_manager_shutdown();
     * streaming_shutdown();
     * filesystem_shutdown();
     * memory_shutdown();
     */

    LOGLN("[boot] gfx2d");
    gfx2d_shutdown();

    LOGLN("[boot] audio");
    audio_shutdown();
    boot->audio_available = 0;

    LOGLN("[boot] resources");
    resources_shutdown();
    LOGLN("[boot] streaming");
    streaming_shutdown();

    LOGLN("[boot] input");
    input_shutdown();
    boot->input_available = 0;

    boot->initialized = 0;

    LOGLN("[boot] platform");
    platform_shutdown();

    LOGLN("[boot] done");

    boot_shutdown_screen(boot);
}
