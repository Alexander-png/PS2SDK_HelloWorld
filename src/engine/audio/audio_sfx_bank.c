#include "engine/audio/audio_sfx_bank.h"
#include "engine/audio/audio_wav.h"
#include "engine/memory/memory.h"

#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef AUDIO_FRAME_BYTES
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)
#endif

int audio_sfx_bank_load_wav(const char *wav_path, audio_sfx_data_t *out)
{
    audio_wav_info_t info;
    int fd;
    int rd;
    s16 *pcm;
    u32 pcm_bytes;

    if (!wav_path || !out)
        return -1;

    memset(out, 0, sizeof(*out));

    if (audio_wav_parse_file(wav_path, &info) < 0)
        return -2;

    if (info.channels != 2 || info.bits != 16)
        return -3;

    if (info.data_size == 0 || info.total_frames == 0)
        return -4;

    pcm_bytes = info.total_frames * AUDIO_FRAME_BYTES;
    if (pcm_bytes != info.data_size)
        return -5;

    pcm = (s16 *)mem_alloc(pcm_bytes, 64, MEMTAG_AUDIO);
    if (!pcm)
        return -6;

    fd = open(wav_path, O_RDONLY);
    if (fd < 0) {
        mem_free(pcm, MEMTAG_AUDIO);
        return -7;
    }

    if (lseek(fd, info.data_offset, SEEK_SET) < 0) {
        close(fd);
        mem_free(pcm, MEMTAG_AUDIO);
        return -8;
    }

    rd = read(fd, pcm, pcm_bytes);
    close(fd);

    if (rd < 0 || (u32)rd != pcm_bytes) {
        mem_free(pcm, MEMTAG_AUDIO);
        return -9;
    }

    out->pcm = pcm;
    out->total_frames = info.total_frames;
    out->src_rate = info.src_rate;
    out->channels = info.channels;
    out->bits = info.bits;
    return 0;
}

void audio_sfx_bank_free(audio_sfx_data_t *data)
{
    if (!data)
        return;

    if (data->pcm)
        mem_free(data->pcm, MEMTAG_AUDIO);

    memset(data, 0, sizeof(*data));
}