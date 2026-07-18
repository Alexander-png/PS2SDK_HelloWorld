#include "boot.h"
#include "game_app.h"
#include "engine/logging/log.h"

int main(void)
{
    boot_context_t boot;
    if (boot_init(&boot) < 0) {
        LOGLNC(LOGCAT_APP, "[main] boot failed");
        boot_shutdown(&boot);
        return 1;
    }

    if (game_app_init() < 0) {
        LOGLNC(LOGCAT_APP, "[main] game_app init failed");
        boot_shutdown(&boot);
        return 1;
    }

    LOGLNC(LOGCAT_APP, "[main] entering main loop");

    while (game_app_is_running())
        game_app_tick();

    game_app_shutdown();

    boot_shutdown(&boot);
    return 0;
}