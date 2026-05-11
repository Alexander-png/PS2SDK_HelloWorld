#include "boot.h"
#include "game_app.h"
#include "engine/logging/log.h"

int main(void)
{
    boot_context_t boot;
    if (boot_init(&boot) < 0) {
        LOGLN("[main] boot failed");
        boot_shutdown(&boot);
        return 1;
    }

    if (game_app_init() < 0) {
        LOGLN("[main] game_app init failed");
        boot_shutdown(&boot);
        return 1;
    }

    LOGLN("[main] entering main loop");

    while (game_app_is_running())
        game_app_tick();

    game_app_shutdown();

    boot_shutdown(&boot);
    return 0;
}

// TODO:
// Fix audio mixer pause behaviour (audio test state): audio never plays after stoped
// Implement resource test state and test resourse and streaming