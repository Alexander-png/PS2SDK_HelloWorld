#include "engine/audio/audio_wav.h"
#include "engine/memory/memory.h"

#include <fcntl.h>
#include <string.h>
#include <unistd.h>

#ifndef WAV_RIFF
#define WAV_RIFF 0x46464952u
#endif

#ifndef WAV_WAVE
#define WAV_WAVE 0x45564157u
#endif

#ifndef WAV_FMT
#define WAV_FMT  0x20746d66u
#endif

#ifndef WAV_DATA
#define WAV_DATA 0x61746164u
#endif

#ifndef AUDIO_FRAME_BYTES
#define AUDIO_FRAME_BYTES (sizeof(s16) * 2)
#endif

static u32 audio_wav_rd32(const u8 *p)
{
    return (u32)p[0]
         | ((u32)p[1] << 8)
         | ((u32)p[2] << 16)
         | ((u32)p[3] << 24);
}

static u16 audio_wav_rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static int audio_wav_parse_fd(int fd, audio_wav_info_t *out)
{
    u8 head[12];
    int found_fmt = 0;
    int found_data = 0;
    u16 audio_format = 0;

    if (!out)
        return -1;

    memset(out, 0, sizeof(*out));

    if (lseek(fd, 0, SEEK_SET) < 0)
        return -2;

    if (read(fd, head, sizeof(head)) != (int)sizeof(head))
        return -3;

    if (audio_wav_rd32(head + 0) != WAV_RIFF ||
        audio_wav_rd32(head + 8) != WAV_WAVE)
        return -4;

    while (1) {
        u8 chunk_hdr[8];
        u32 id, chunk_size;
        off_t chunk_data_pos;

        if (read(fd, chunk_hdr, sizeof(chunk_hdr)) != (int)sizeof(chunk_hdr))
            break;

        id = audio_wav_rd32(chunk_hdr + 0);
        chunk_size = audio_wav_rd32(chunk_hdr + 4);

        chunk_data_pos = lseek(fd, 0, SEEK_CUR);
        if (chunk_data_pos < 0)
            return -5;

        if (id == WAV_FMT) {
            u8 fmtbuf[40];

            if (chunk_size < 16)
                return -6;

            if (chunk_size > sizeof(fmtbuf)) {
                if (read(fd, fmtbuf, sizeof(fmtbuf)) != (int)sizeof(fmtbuf))
                    return -7;
                if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                    return -8;
            } else {
                if (read(fd, fmtbuf, chunk_size) != (int)chunk_size)
                    return -7;
                if (chunk_size & 1) {
                    if (lseek(fd, 1, SEEK_CUR) < 0)
                        return -8;
                }
            }

            audio_format   = audio_wav_rd16(fmtbuf + 0);
            out->channels  = audio_wav_rd16(fmtbuf + 2);
            out->src_rate  = (int)audio_wav_rd32(fmtbuf + 4);
            out->bits      = audio_wav_rd16(fmtbuf + 14);
            found_fmt = 1;
        }
        else if (id == WAV_DATA) {
            out->data_offset = (u32)chunk_data_pos;
            out->data_size   = chunk_size;

            if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                return -9;

            found_data = 1;
        }
        else {
            if (lseek(fd, chunk_data_pos + chunk_size + (chunk_size & 1), SEEK_SET) < 0)
                return -10;
        }

        if (found_fmt && found_data)
            break;
    }

    if (!found_fmt || !found_data)
        return -11;
    if (audio_format != 1)
        return -12;

    if (audio_wav_validate_pcm16stereo(out) < 0)
        return -13;

    out->total_frames = out->data_size / AUDIO_FRAME_BYTES;
    return 0;
}

int audio_wav_validate_pcm16stereo(const audio_wav_info_t *info)
{
    if (!info)
        return -1;
    if (info->channels != 2)
        return -2;
    if (info->bits != 16)
        return -3;
    if (info->src_rate <= 0)
        return -4;
    if ((info->data_size & (AUDIO_FRAME_BYTES - 1)) != 0)
        return -5;
    return 0;
}

int audio_wav_parse_file(const char *path, audio_wav_info_t *out)
{
    int fd;
    int rc;

    if (!path || !out)
        return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -2;

    rc = audio_wav_parse_fd(fd, out);
    close(fd);
    return rc;
}

int audio_wav_load_file_pcm16s(const char *path,
                               s16 **out_pcm,
                               audio_wav_info_t *out)
{
    int fd;
    int rc;
    audio_wav_info_t info;
    u8 *pcm_bytes = NULL;
    u32 total_read = 0;

    if (!path || !out_pcm || !out)
        return -1;

    *out_pcm = NULL;
    memset(&info, 0, sizeof(info));

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -2;

    rc = audio_wav_parse_fd(fd, &info);
    if (rc < 0) {
        close(fd);
        return rc;
    }

    pcm_bytes = (u8 *)mem_alloc(info.data_size, 64, MEMTAG_AUDIO);
    if (!pcm_bytes) {
        close(fd);
        return -20;
    }

    if (lseek(fd, info.data_offset, SEEK_SET) < 0) {
        mem_free(pcm_bytes, MEMTAG_AUDIO);
        close(fd);
        return -21;
    }

    while (total_read < info.data_size) {
        int got = read(fd, pcm_bytes + total_read, info.data_size - total_read);
        if (got <= 0) {
            mem_free(pcm_bytes, MEMTAG_AUDIO);
            close(fd);
            return -22;
        }
        total_read += (u32)got;
    }

    close(fd);

    *out_pcm = (s16 *)pcm_bytes;
    *out = info;
    return 0;
}