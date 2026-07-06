#include "platform.h"
#include "engine/logging/log.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <delaythread.h>
#include <sbv_patches.h>
#include <timer.h>

static int s_platform_ready = 0;
static int s_timer_system_started = 0;

static unsigned long long platform_ps2_timer_to_us(u64 timer_clocks)
{
    u32 sec;
    u32 usec;

    sec = 0;
    usec = 0;
    TimerBusClock2USec(timer_clocks, &sec, &usec);

    return ((unsigned long long)sec * 1000000ULL) +
           (unsigned long long)usec;
}

int platform_init(void)
{
    int rc;

    if (s_platform_ready)
        return 0;

    LOGLN("[platform] init");

    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();

    rc = StartTimerSystemTime();
    if (rc < 0) {
        LOGLN("[platform] StartTimerSystemTime failed rc=%d", rc);
        return rc;
    }

    s_timer_system_started = 1;
    s_platform_ready = 1;

    LOGLN("[platform] SIF/RPC/loadfile/timer ready");
    return 0;
}

void platform_shutdown(void)
{
    if (!s_platform_ready)
        return;

    if (s_timer_system_started) {
        StopTimerSystemTime();
        s_timer_system_started = 0;
    }

    /*
     * Usually nothing important here yet.
     * Later you can add pad shutdown, filesystem shutdown, etc.
     */

    s_platform_ready = 0;
    LOGLN("[platform] shutdown");
}

void platform_delay_us(int usec)
{
    if (usec <= 0)
        return;

    DelayThread(usec);
}

unsigned int platform_ticks_ms(void)
{
    return (unsigned int)(platform_time_now_us() / 1000ULL);
}

unsigned long long platform_time_now_us(void)
{
    u64 now;

    if (!s_platform_ready || !s_timer_system_started)
        return 0;

    now = GetTimerSystemTime();
    return platform_ps2_timer_to_us(now);
}

float platform_time_now_seconds(void)
{
    return (float)platform_time_now_us() / 1000000.0f;
}

void platform_yield(void)
{
    DelayThread(0);
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