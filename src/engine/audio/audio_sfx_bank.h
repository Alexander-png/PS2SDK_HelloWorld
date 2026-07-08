#ifndef AUDIO_SFX_BANK_H
#define AUDIO_SFX_BANK_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_sfx_data {
    s16 *pcm;
    u32 total_frames;
    u32 src_rate;
    int channels;
    int bits;
} audio_sfx_data_t;

int  audio_sfx_bank_load_wav(const char *wav_path, audio_sfx_data_t *out);
void audio_sfx_bank_free(audio_sfx_data_t *data);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_SFX_BANK_H */