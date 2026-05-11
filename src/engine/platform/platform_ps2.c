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

int platform_load_module(const char *path, int argc, const char *argv)
{
    if (!s_platform_ready)
        return -9999;

    return SifLoadModule(path, argc, argv);
}

int platform_exec_module_buffer(const void *buffer,
                                unsigned int size,
                                int argc,
                                const char *argv,
                                int *result)
{
    if (!s_platform_ready)
        return -9999;

    return SifExecModuleBuffer((void *)buffer, size, argc, argv, result);
}
