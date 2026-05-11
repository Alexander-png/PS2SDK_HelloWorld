#ifndef AUDIO_DRIVER_H
#define AUDIO_DRIVER_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    AUDIO_DRIVER_OK                 = 0,
    AUDIO_DRIVER_ERR_LIBSD          = -1,
    AUDIO_DRIVER_ERR_AUDSRV_MODULE  = -2,
    AUDIO_DRIVER_ERR_AUDSRV_INIT    = -3,
    AUDIO_DRIVER_ERR_FORMAT         = -4
};

int  audio_driver_init(void);
void audio_driver_shutdown(void);
int  audio_driver_is_ready(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_DRIVER_H */
