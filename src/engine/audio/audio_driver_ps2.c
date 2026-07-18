#include "audio_driver.h"
#include "engine/logging/log.h"
#include "engine/platform/platform.h"

#include <tamtypes.h>
#include <audsrv.h>

#ifndef AUDIO_OUTPUT_RATE
#define AUDIO_OUTPUT_RATE 48000
#endif

#ifndef AUDIO_OUTPUT_BITS
#define AUDIO_OUTPUT_BITS 16
#endif

#ifndef AUDIO_OUTPUT_CHANNELS
#define AUDIO_OUTPUT_CHANNELS 2
#endif

#ifndef AUDIO_MASTER_VOLUME
#define AUDIO_MASTER_VOLUME MAX_VOLUME
#endif

#ifndef AUDIO_AUDSRV_STARTUP_DELAY_US
#define AUDIO_AUDSRV_STARTUP_DELAY_US 100000
#endif

extern unsigned char audsrv_irx[];
extern unsigned int size_audsrv_irx;

static int s_audio_driver_ready = 0;

int audio_driver_init(void)
{
    int irx_ret;
    int mod_ret;
    int mod_id;
    audsrv_fmt_t fmt;

    s_audio_driver_ready = 0;

    LOGLNC(LOGCAT_AUDIO, ("[audio] driver init"));

    irx_ret = platform_load_module("rom0:LIBSD", 0, NULL);
    LOGLNC(LOGCAT_AUDIO, "[audio] LIBSD load: %d", irx_ret);

    if (irx_ret < 0) {
        LOGLNC(LOGCAT_AUDIO, "[audio] LIBSD load failed: %d", irx_ret);
        return AUDIO_DRIVER_ERR_LIBSD;
    }

    mod_ret = 0;
    mod_id = platform_exec_module_buffer(
        audsrv_irx,
        size_audsrv_irx,
        0,
        NULL,
        &mod_ret
    );

    LOGLNC(LOGCAT_AUDIO, "[audio] audsrv.irx exec id=%d ret=%d", mod_id, mod_ret);

    if (mod_id < 0 || mod_ret < 0) {
        LOGLNC(LOGCAT_AUDIO, "[audio] audsrv.irx exec failed: id=%d ret=%d", mod_id, mod_ret);
        return AUDIO_DRIVER_ERR_AUDSRV_MODULE;
    }

    /*
     * Give the IOP module a short chance to create its RPC listener thread
     * before the EE-side audsrv_init() tries to bind to it.
     */
    platform_delay_us(AUDIO_AUDSRV_STARTUP_DELAY_US);
    LOGLNC(LOGCAT_AUDIO, "[audio] audsrv.irx startup delay done");

    LOGLNC(LOGCAT_AUDIO, "[audio] calling audsrv_init");
    if (audsrv_init() != 0) {
        LOGLNC(LOGCAT_AUDIO, "[audio] audsrv_init failed");
        return AUDIO_DRIVER_ERR_AUDSRV_INIT;
    }
    LOGLNC(LOGCAT_AUDIO, "[audio] audsrv_init ok");

    fmt.freq = AUDIO_OUTPUT_RATE;
    fmt.bits = AUDIO_OUTPUT_BITS;
    fmt.channels = AUDIO_OUTPUT_CHANNELS;

    if (audsrv_set_format(&fmt) != 0) {
        LOGLNC(LOGCAT_AUDIO, "[audio] audsrv_set_format failed");
        return AUDIO_DRIVER_ERR_FORMAT;
    }

    audsrv_set_volume(AUDIO_MASTER_VOLUME);

    s_audio_driver_ready = 1;

    LOGLNC(LOGCAT_AUDIO, "[audio] audsrv ready: %d Hz stereo s16", AUDIO_OUTPUT_RATE);
    return AUDIO_DRIVER_OK;
}

void audio_driver_shutdown(void)
{
    if (!s_audio_driver_ready)
        return;

    audsrv_stop_audio();
    s_audio_driver_ready = 0;

    LOGLNC(LOGCAT_AUDIO, "[audio] driver shutdown");
}

int audio_driver_is_ready(void)
{
    return s_audio_driver_ready;
}
