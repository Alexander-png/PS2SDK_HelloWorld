#include "audio_player.h"

#include <audsrv.h>
#include <kernel.h>
#include <delaythread.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#ifndef AUDIO_PLAYER_THREAD_PRIO
#define AUDIO_PLAYER_THREAD_PRIO 32
#endif

#ifndef AUDIO_PLAYER_STACK_SIZE
#define AUDIO_PLAYER_STACK_SIZE 0x2000
#endif

#ifndef AUDIO_PLAYER_IDLE_US
#define AUDIO_PLAYER_IDLE_US 2000
#endif

#define WAV_RIFF 0x46464952u
#define WAV_WAVE 0x45564157u
#define WAV_FMT  0x20746d66u
#define WAV_DATA 0x61746164u
#define AUDIO_OUTPUT_RATE 48000
#define AUDIO_VOL_MAX 0x3fff

static u32 rd32(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static u16 rd16(const u8 *p)
{
    return (u16)p[0] | ((u16)p[1] << 8);
}

static int load_file(const char *path, u8 **out_buf, u32 *out_size)
{
    int fd;
    int size;
    u8 *buf;
    int rd;

    if (!path || !out_buf || !out_size)
        return -1;

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return -2;

    size = lseek(fd, 0, SEEK_END);
    lseek(fd, 0, SEEK_SET);
    if (size <= 0) {
        close(fd);
        return -3;
    }

    buf = (u8 *)memalign(64, (size_t)size);
    if (!buf) {
        close(fd);
        return -4;
    }

    rd = read(fd, buf, size);
    close(fd);
    if (rd != size) {
        free(buf);
        return -5;
    }

    *out_buf = buf;
    *out_size = (u32)size;
    return 0;
}

static int parse_wav(audio_player_t *p)
{
    const u8 *buf = p->file_data;
    u32 size = p->file_size;
    u32 pos = 12;
    int found_fmt = 0;
    int found_data = 0;
    u16 audio_format = 0;

    if (size < 12)
        return -10;

    if (rd32(buf + 0) != WAV_RIFF || rd32(buf + 8) != WAV_WAVE)
        return -11;

    while (pos + 8 <= size) {
        u32 id = rd32(buf + pos + 0);
        u32 chunk_size = rd32(buf + pos + 4);
        u32 chunk_data = pos + 8;
        u32 next = chunk_data + chunk_size;

        if (next > size)
            return -12;

        if (id == WAV_FMT) {
            if (chunk_size < 16)
                return -13;
            audio_format = rd16(buf + chunk_data + 0);
            p->channels   = rd16(buf + chunk_data + 2);
            p->src_rate   = (int)rd32(buf + chunk_data + 4);
            p->bits       = rd16(buf + chunk_data + 14);
            found_fmt = 1;
        } else if (id == WAV_DATA) {
            p->pcm_data  = p->file_data + chunk_data;
            p->pcm_bytes = chunk_size;
            found_data = 1;
        }

        pos = next + (chunk_size & 1);
    }

    if (!found_fmt || !found_data)
        return -14;
    if (audio_format != 1)
        return -15;
    if (p->channels != 2)
        return -16;
    if (p->bits != 16)
        return -17;
    if (p->src_rate <= 0)
        return -18;

    return 0;
}

static void fill_chunk(audio_player_t *p, s16 *out, int out_frames)
{
    const s16 *src = (const s16 *)p->pcm_data;
    const u32 total_frames = p->pcm_bytes / (sizeof(s16) * 2);
    const double rate_ratio = (double)p->src_rate / (double)AUDIO_OUTPUT_RATE;
    int i;

    if (!src || total_frames == 0) {
        memset(out, 0, out_frames * 2 * sizeof(s16));
        return;
    }

    for (i = 0; i < out_frames; i++) {
        int idx0, idx1;
        float t;
        s32 l, r;

        if (!p->playing || p->paused) {
            out[i * 2 + 0] = 0;
            out[i * 2 + 1] = 0;
            continue;
        }

        while (p->src_pos >= (double)total_frames) {
            if (p->loop) {
                p->src_pos -= (double)total_frames;
            } else {
                p->playing = 0;
                out[i * 2 + 0] = 0;
                out[i * 2 + 1] = 0;
                goto next_sample;
            }
        }

        if (p->src_pos < 0.0)
            p->src_pos = 0.0;

        idx0 = (int)p->src_pos;
        idx1 = idx0 + 1;
        if (idx1 >= (int)total_frames)
            idx1 = p->loop ? 0 : idx0;

        t = (float)(p->src_pos - (double)idx0);

        l = (s32)((1.0f - t) * src[idx0 * 2 + 0] + t * src[idx1 * 2 + 0]);
        r = (s32)((1.0f - t) * src[idx0 * 2 + 1] + t * src[idx1 * 2 + 1]);

        if (l < -32768) l = -32768;
        if (l >  32767) l =  32767;
        if (r < -32768) r = -32768;
        if (r >  32767) r =  32767;

        out[i * 2 + 0] = (s16)l;
        out[i * 2 + 1] = (s16)r;
        p->src_pos += (double)p->speed * rate_ratio;
        continue;

next_sample:
        ;
    }
}

static void audio_thread(void *arg)
{
    audio_player_t *p = (audio_player_t *)arg;
    const int bytes = p->mixbuf_frames * 2 * (int)sizeof(s16);

    p->thread_running = 1;

    while (!p->thread_exit) {
        if (!p->playing) {
            DelayThread(AUDIO_PLAYER_IDLE_US);
            continue;
        }

        if (p->paused) {
            memset(p->mixbuf, 0, bytes);
            audsrv_wait_audio(bytes);
            if (p->thread_exit)
                break;
            audsrv_play_audio((const char *)p->mixbuf, bytes);
            continue;
        }

        fill_chunk(p, p->mixbuf, p->mixbuf_frames);
        audsrv_wait_audio(bytes);
        if (p->thread_exit)
            break;
        audsrv_play_audio((const char *)p->mixbuf, bytes);
    }

    p->thread_running = 0;
    ExitDeleteThread();
}

int audio_player_init(audio_player_t *p, const char *wav_path, int mixbuf_frames)
{
    int ret;
    ee_thread_t th;

    if (!p || !wav_path)
        return -1;

    memset(p, 0, sizeof(*p));
    p->volume = 100;
    p->speed = 1.0f;
    p->thread_id = -1;

    ret = load_file(wav_path, &p->file_data, &p->file_size);
    if (ret < 0)
        return ret;

    ret = parse_wav(p);
    if (ret < 0) {
        free(p->file_data);
        p->file_data = NULL;
        p->file_size = 0;
        return ret;
    }

    if (mixbuf_frames <= 0)
        mixbuf_frames = 1024;

    p->mixbuf = (s16 *)memalign(64, (size_t)(mixbuf_frames * 2 * sizeof(s16)));
    if (!p->mixbuf) {
        free(p->file_data);
        p->file_data = NULL;
        p->file_size = 0;
        return -20;
    }
    p->mixbuf_frames = mixbuf_frames;

    p->thread_stack = memalign(16, AUDIO_PLAYER_STACK_SIZE);
    if (!p->thread_stack) {
        free(p->mixbuf);
        free(p->file_data);
        memset(p, 0, sizeof(*p));
        return -21;
    }

    memset(&th, 0, sizeof(th));
    th.func = audio_thread;
    th.stack = p->thread_stack;
    th.stack_size = AUDIO_PLAYER_STACK_SIZE;
    th.gp_reg = &_gp;
    th.initial_priority = AUDIO_PLAYER_THREAD_PRIO;

    p->thread_id = CreateThread(&th);
    if (p->thread_id < 0) {
        free(p->thread_stack);
        free(p->mixbuf);
        free(p->file_data);
        memset(p, 0, sizeof(*p));
        return -22;
    }

    StartThread(p->thread_id, p);
    audio_player_set_volume(p, 100);
    return 0;
}

void audio_player_destroy(audio_player_t *p)
{
    int i;

    if (!p)
        return;

    audio_player_stop(p);
    p->thread_exit = 1;
    audsrv_stop_audio();

    for (i = 0; i < 100 && p->thread_running; i++)
        DelayThread(1000);

    if (p->thread_running && p->thread_id >= 0) {
        TerminateThread(p->thread_id);
        DeleteThread(p->thread_id);
    }

    if (p->thread_stack)
        free(p->thread_stack);
    if (p->mixbuf)
        free(p->mixbuf);
    if (p->file_data)
        free(p->file_data);

    memset(p, 0, sizeof(*p));
    p->thread_id = -1;
}

int audio_player_play(audio_player_t *p, int loop)
{
    if (!p || !p->pcm_data)
        return -1;

    p->loop = loop ? 1 : 0;
    p->playing = 1;
    p->paused = 0;
    return 0;
}

void audio_player_pause(audio_player_t *p)
{
    if (!p)
        return;

    p->paused = 1;
    audsrv_stop_audio();
}

void audio_player_resume(audio_player_t *p)
{
    if (!p)
        return;

    if (p->playing)
        p->paused = 0;
}

void audio_player_stop(audio_player_t *p)
{
    if (!p)
        return;

    p->playing = 0;
    p->paused = 0;
    p->src_pos = 0.0;
    audsrv_stop_audio();
}

void audio_player_set_volume(audio_player_t *p, int percent)
{
    int vol;

    if (!p)
        return;

    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;

    p->volume = percent;
    vol = (percent * AUDIO_VOL_MAX) / 100;
    audsrv_set_volume(vol);
}

void audio_player_set_speed(audio_player_t *p, float speed)
{
    if (!p)
        return;

    if (speed < (1.0f / 3.0f)) speed = (1.0f / 3.0f);
    if (speed > 3.0f) speed = 3.0f;
    p->speed = speed;
}

int audio_player_is_playing(const audio_player_t *p)
{
    return p ? p->playing : 0;
}

int audio_player_is_paused(const audio_player_t *p)
{
    return p ? p->paused : 0;
}