#include <unistd.h>
#include <kernel.h>
#include <debug.h>
#include <graph.h>

#include "logging/log.h"

int main(void)
{
    log_init();
    init_scr();
    sleep(3);

    LOGLN("Start test...");

    for (int i = 0; i < 100; ++i) {
        LOG("Test log %d\n", i);
        graph_wait_vsync();
    }

    LOGLN("Done. Sleeping thread.");
    SleepThread();
    return 0;
}