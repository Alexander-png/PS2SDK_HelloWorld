#ifndef AUDIO_WAV_H
#define AUDIO_WAV_H

#include <tamtypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct audio_wav_info {
    u32 data_offset;
    u32 data_size;
    u32 total_frames;

    int src_rate;
    int channels;
    int bits;
} audio_wav_info_t;

/* Parse WAV metadata only. Supports current engine constraints:
   PCM, stereo, 16-bit. */
int audio_wav_parse_file(const char *path, audio_wav_info_t *out);

/* Utility validation shared by stream and sfx paths. */
int audio_wav_validate_pcm16stereo(const audio_wav_info_t *info);

/* Load full PCM payload into memory.
   out_pcm is allocated with mem_alloc(..., MEMTAG_AUDIO). */
int audio_wav_load_file_pcm16s(const char *path,
                               s16 **out_pcm,
                               audio_wav_info_t *out);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_WAV_H */