/* Small WAV reader/writer (PCM16, PCM32 and IEEE float32, mono or stereo). */
#include "dtsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

typedef struct {
    int channels;
    int bits;
    int is_float;
    long rate;
    size_t frames;
    float *data; /* interleaved, one float per sample */
} DtsxAudio;

int dtsx_wav_read(const char *path, int *ch_out, long *rate_out,
                  size_t *frames_out, float **data_out);

static int dtsx_audio_read(const char *path, DtsxAudio *a)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    char id[4];
    uint32_t sz;
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4)) { fclose(f); return -1; }
    if (fread(&sz, 4, 1, f) != 1) { fclose(f); return -1; }
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4)) { fclose(f); return -1; }

    int fmt = 0, ch = 0, bits = 0;
    long rate = 0;
    size_t data_bytes = 0;
    long data_off = -1;
    while (fread(id, 1, 4, f) == 4) {
        uint32_t n;
        if (fread(&n, 4, 1, f) != 1) break;
        if (!memcmp(id, "fmt ", 4)) {
            if (n < 16) { fclose(f); return -1; }
            uint16_t af, c, blk, bps;
            uint32_t rt, br;
            if (fread(&af, 2, 1, f) != 1 || fread(&c, 2, 1, f) != 1 ||
                fread(&rt, 4, 1, f) != 1 || fread(&br, 4, 1, f) != 1 ||
                fread(&blk, 2, 1, f) != 1 || fread(&bps, 2, 1, f) != 1) {
                fclose(f); return -1;
            }
            (void)br; (void)blk;
            fmt = af; ch = c; rate = (long)rt; bits = bps;
            if (n > 16) fseek(f, (long)(n - 16), SEEK_CUR);
        } else if (!memcmp(id, "data", 4)) {
            data_off = ftell(f);
            data_bytes = n;
            break;
        } else {
            fseek(f, (long)n + (long)(n & 1), SEEK_CUR);
        }
    }
    if (data_off < 0 || ch < 1 || ch > 2 || bits < 8) { fclose(f); return -1; }
    const int is_float = (fmt == 3);
    if (fmt != 1 && fmt != 3) { fclose(f); return -1; }
    if (is_float && bits != 32) { fclose(f); return -1; }
    if (!is_float && bits != 16 && bits != 32 && bits != 24) { fclose(f); return -1; }

    const size_t bps = (size_t)bits / 8;
    const size_t frame_bytes = bps * (size_t)ch;
    a->frames = data_bytes / frame_bytes;
    a->channels = ch;
    a->bits = bits;
    a->is_float = is_float;
    a->rate = rate;
    a->data = (float *)calloc(a->frames * (size_t)ch, sizeof(float));
    if (!a->data) { fclose(f); return -1; }
    fseek(f, data_off, SEEK_SET);
    for (size_t i = 0; i < a->frames * (size_t)ch; i++) {
        if (is_float) {
            float v;
            if (fread(&v, 4, 1, f) != 1) break;
            a->data[i] = v;
        } else if (bits == 16) {
            int16_t v;
            if (fread(&v, 2, 1, f) != 1) break;
            a->data[i] = (float)v / 32768.0f;
        } else if (bits == 24) {
            unsigned char b[3];
            if (fread(b, 1, 3, f) != 3) break;
            int32_t v = (int32_t)((uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16));
            if (v & 0x800000) v |= (int32_t)0xff000000;
            a->data[i] = (float)v / 8388608.0f;
        } else {
            int32_t v;
            if (fread(&v, 4, 1, f) != 1) break;
            a->data[i] = (float)((double)v / 2147483648.0);
        }
    }
    fclose(f);
    return 0;
}

static int dtsx_audio_write16(const char *path, const DtsxAudio *a)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t data = (uint32_t)(a->frames * (size_t)a->channels * 2);
    const uint32_t riff = 36 + data;
    const uint16_t ch = (uint16_t)a->channels, bits = 16, block = (uint16_t)(a->channels * 2);
    const uint32_t rate = (uint32_t)a->rate, brate = rate * block;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f); { uint32_t n = 16; fwrite(&n, 4, 1, f); }
    { uint16_t pcm = 1; fwrite(&pcm, 2, 1, f); }
    fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f);
    fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    unsigned int dither = 0x1234567u;
    for (size_t i = 0; i < a->frames * (size_t)a->channels; i++) {
        float v = a->data[i];
        if (!isfinite(v)) v = 0.0f;
        if (v > 1.0f) v = 1.0f;
        if (v < -1.0f) v = -1.0f;
        dither ^= dither << 13; dither ^= dither >> 17; dither ^= dither << 5;
        const float d = ((float)(dither & 0xffff) / 65536.0f - 0.5f) / 32768.0f;
        int32_t s = (int32_t)lrintf((v + d) * 32767.0f);
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        const int16_t o = (int16_t)s;
        fwrite(&o, 2, 1, f);
    }
    fclose(f);
    return 0;
}

int dtsx_wav_read(const char *path, int *ch_out, long *rate_out,
                  size_t *frames_out, float **data_out)
{
    DtsxAudio a;
    memset(&a, 0, sizeof(a));
    if (dtsx_audio_read(path, &a) != 0) return -1;
    *ch_out = a.channels;
    *rate_out = a.rate;
    *frames_out = a.frames;
    *data_out = a.data;
    return 0;
}

int dtsx_wav_write16(const char *path, int channels, long rate,
                     size_t frames, const float *data)
{
    DtsxAudio a;
    a.channels = channels;
    a.bits = 16;
    a.is_float = 0;
    a.rate = rate;
    a.frames = frames;
    a.data = (float *)data;
    return dtsx_audio_write16(path, &a);
}
