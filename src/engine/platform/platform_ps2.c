#include "engine/platform/platform.h"
#include "engine/logging/log.h"

#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
#include <delaythread.h>
#include <sbv_patches.h>
#include <timer.h>

#define PLATFORM_PS2_MAIN_THREAD_PRIO 36
#define PLATFORM_PS2_YIELD_US         100

static int s_platform_ready = 0;
static int s_timer_system_started = 0;

static int s_main_thread_id = -1;
static int s_main_thread_priority_changed = 0;

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

static int platform_ps2_configure_main_thread(void)
{
    int thread_id;
    int rc;

    thread_id = GetThreadId();
    if (thread_id < 0) {
        LOGLNC(LOGCAT_PLATFORM,
               "[platform] GetThreadId failed rc=%d", thread_id);
        return thread_id;
    }

    rc = ChangeThreadPriority(thread_id, PLATFORM_PS2_MAIN_THREAD_PRIO);
    if (rc < 0) {
        LOGLNC(LOGCAT_PLATFORM,
               "[platform] ChangeThreadPriority tid=%d prio=%d failed rc=%d",
               thread_id,
               PLATFORM_PS2_MAIN_THREAD_PRIO,
               rc);
        return rc;
    }

    s_main_thread_id = thread_id;
    s_main_thread_priority_changed = 1;

    LOGLNC(LOGCAT_PLATFORM,
           "[platform] game thread tid=%d priority=%d",
           s_main_thread_id,
           PLATFORM_PS2_MAIN_THREAD_PRIO);

    return 0;
}

int platform_init(void)
{
    int rc;

    if (s_platform_ready)
        return 0;

    LOGLNC(LOGCAT_PLATFORM, "[platform] init");

    SifInitRpc(0);
    SifLoadFileInit();
    sbv_patch_enable_lmb();

    rc = platform_ps2_configure_main_thread();
    if (rc < 0)
        return rc;

    rc = StartTimerSystemTime();
    if (rc < 0) {
        LOGLNC(LOGCAT_PLATFORM, "[platform] StartTimerSystemTime failed rc=%d", rc);
        return rc;
    }

    s_timer_system_started = 1;
    s_platform_ready = 1;

    LOGLNC(LOGCAT_PLATFORM, "[platform] SIF/RPC/loadfile/timer ready");
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
    LOGLNC(LOGCAT_PLATFORM, "[platform] shutdown");
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
    DelayThread(PLATFORM_PS2_YIELD_US);
}

int platform_get_memory_info(platform_memory_info_t *out)
{
    if (!out)
        return -1;

    out->total_physical_bytes = 0;
    out->available_physical_bytes = 0;
    out->has_total_physical = 0;
    out->has_available_physical = 0;

    out->total_physical_bytes = (unsigned int)GetMemorySize();
    out->has_total_physical = 1;

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