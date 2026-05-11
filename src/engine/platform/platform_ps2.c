#include "platform.h"
#include "engine/logging/log.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <delaythread.h>
#include <sbv_patches.h>

static int s_platform_ready = 0;

int platform_init(void)
{
    if (s_platform_ready)
        return 0;

    LOGLN("[platform] init");

    SifInitRpc(0);
    SifLoadFileInit(); /* enable SifLoadModule */
    sbv_patch_enable_lmb();

    s_platform_ready = 1;

    LOGLN("[platform] SIF/RPC/loadfile ready");
    return 0;
}

void platform_shutdown(void)
{
    if (!s_platform_ready)
        return;

    /*
     * Usually nothing important here yet.
     * Later you can add pad shutdown, filesystem shutdown, etc.
     */

    s_platform_ready = 0;
    LOGLN("[platform] shutdown");
}

void platform_delay_us(int usec)
{
    if (usec > 0)
        DelayThread(usec);
}

unsigned int platform_ticks_ms(void)
{
    /*
     * Placeholder.
     * Later: implement with timer or graph/vblank timing.
     */
    return 0;
}