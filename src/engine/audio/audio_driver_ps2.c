#include "audio_driver.h"
#include "log.h"

#include <tamtypes.h>
#include <kernel.h>
#include <sifrpc.h>
#include <loadfile.h>
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

    LOGLN("[audio] driver init");

    irx_ret = SifLoadModule("rom0:LIBSD", 0, NULL);
    LOGLN("[audio] LIBSD load: %d", irx_ret);

    if (irx_ret < 0) {
        LOGLN("[audio] LIBSD load failed: %d", irx_ret);
        return AUDIO_DRIVER_ERR_LIBSD;
    }

    mod_ret = 0;
    mod_id = SifExecModuleBuffer(
        audsrv_irx,
        size_audsrv_irx,
        0,
        NULL,
        &mod_ret
    );

    LOGLN("[audio] audsrv.irx exec id=%d ret=%d", mod_id, mod_ret);

    if (mod_id < 0 || mod_ret < 0) {
        LOGLN("[audio] audsrv.irx exec failed: id=%d ret=%d", mod_id, mod_ret);
        return AUDIO_DRIVER_ERR_AUDSRV_MODULE;
    }

    if (audsrv_init() != 0) {
        LOGLN("[audio] audsrv_init failed");
        return AUDIO_DRIVER_ERR_AUDSRV_INIT;
    }

    fmt.freq = AUDIO_OUTPUT_RATE;
    fmt.bits = AUDIO_OUTPUT_BITS;
    fmt.channels = AUDIO_OUTPUT_CHANNELS;

    
    if (audsrv_set_format(&fmt) != 0) {
        LOGLN("[audio] audsrv_set_format failed");
        return AUDIO_DRIVER_ERR_FORMAT;
    }

    audsrv_set_volume(AUDIO_MASTER_VOLUME);

    s_audio_driver_ready = 1;

    LOGLN("[audio] audsrv ready: %d Hz stereo s16", AUDIO_OUTPUT_RATE);
    return AUDIO_DRIVER_OK;
}

void audio_driver_shutdown(void)
{
    if (!s_audio_driver_ready)
        return;

    audsrv_stop_audio();
    s_audio_driver_ready = 0;

    LOGLN("[audio] driver shutdown");
}

int audio_driver_is_ready(void)
{
    return s_audio_driver_ready;
}
