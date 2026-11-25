/* dtsdsp — CLI front end for the independent DTS:X Ultra-inspired chain.
 *
 *   dtsdsp render -p configs/speakers-music.json -i in.wav -o out.wav
 *   dtsdsp stream -p configs/speakers-music.json [-r 48000]
 *   dtsdsp info   -p configs/speakers-music.json
 *   dtsdsp bench  -p configs/speakers-music.json
 *   dtsdsp selftest
 */
#define _POSIX_C_SOURCE 200809L
#include "dtsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdint.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined(__x86_64__) && defined(__SSE__)
#include <xmmintrin.h>
static void enable_ftz(void)
{
    _mm_setcsr(_mm_getcsr() | 0x8040u); /* DAZ + FTZ */
}
#else
static void enable_ftz(void) {}
#endif

int dtsx_wav_read(const char *path, int *ch_out, long *rate_out,
                  size_t *frames_out, float **data_out);
int dtsx_wav_write16(const char *path, int channels, long rate,
                     size_t frames, const float *data);

static int load_cfg(DtsxConfig *c, const char *path)
{
    if (dtsx_config_defaults(c, 48000.0) != 0) return -1;
    if (path && dtsx_config_load(c, path) != 0) {
        fprintf(stderr, "dtsdsp: cannot read config %s\n", path);
        return -1;
    }
    return 0;
}

static int cmd_render(const char *cfgpath, const char *in, const char *out)
{
    DtsxConfig c;
    if (load_cfg(&c, cfgpath) != 0) return 2;
    int ch = 0;
    long rate = 0;
    size_t frames = 0;
    float *data = NULL;
    if (dtsx_wav_read(in, &ch, &rate, &frames, &data) != 0) {
        fprintf(stderr, "dtsdsp: cannot read %s\n", in);
        return 2;
    }
    if (rate <= 0) rate = 48000;
    DtsxChain *chain = dtsx_chain_create(&c, (double)rate);
    if (!chain) { free(data); return 2; }

    float *l = (float *)malloc(sizeof(float) * 1024);
    float *r = (float *)malloc(sizeof(float) * 1024);
    if (!l || !r) { free(l); free(r); free(data); dtsx_chain_destroy(chain); return 2; }

    size_t pos = 0;
    while (pos < frames) {
        size_t n = frames - pos;
        if (n > 1024) n = 1024;
        for (size_t i = 0; i < n; i++) {
            l[i] = data[(pos + i) * (size_t)ch];
            r[i] = (ch > 1) ? data[(pos + i) * (size_t)ch + 1] : l[i];
        }
        dtsx_chain_process(chain, l, r, n);
        for (size_t i = 0; i < n; i++) {
            data[(pos + i) * (size_t)ch] = l[i];
            if (ch > 1) data[(pos + i) * (size_t)ch + 1] = r[i];
        }
        pos += n;
    }
    free(l); free(r);
    int rc = dtsx_wav_write16(out, ch, rate, frames, data);
    free(data);
    dtsx_chain_destroy(chain);
    if (rc != 0) fprintf(stderr, "dtsdsp: cannot write %s\n", out);
    return rc == 0 ? 0 : 2;
}

static int cmd_stream(const char *cfgpath, double rate)
{
    DtsxConfig c;
    if (load_cfg(&c, cfgpath) != 0) return 2;
    if (rate <= 0) rate = c.fs > 0 ? c.fs : 48000.0;
    DtsxChain *chain = dtsx_chain_create(&c, rate);
    if (!chain) return 2;
    float buf[2 * 256];
    size_t n;
    while ((n = fread(buf, sizeof(float) * 2, 256, stdin)) > 0) {
        float l[256], r[256];
        for (size_t i = 0; i < n; i++) { l[i] = buf[2 * i]; r[i] = buf[2 * i + 1]; }
        dtsx_chain_process(chain, l, r, n);
        for (size_t i = 0; i < n; i++) { buf[2 * i] = l[i]; buf[2 * i + 1] = r[i]; }
        if (fwrite(buf, sizeof(float) * 2, n, stdout) != n) break;
        fflush(stdout);
    }
    dtsx_chain_destroy(chain);
    return 0;
}

static int cmd_info(const char *cfgpath)
{
    DtsxConfig c;
    if (load_cfg(&c, cfgpath) != 0) return 2;
    printf("profile: %s\n", cfgpath ? cfgpath : "(defaults)");
    dtsx_config_print(&c, stdout);
    DtsxChain *ch = dtsx_chain_create(&c, c.fs);
    if (!ch) { fprintf(stderr, "dtsdsp: chain creation failed\n"); return 2; }
    printf("latency: %u samples (%.2f ms at %g Hz)\n",
           dtsx_chain_latency(ch), 1000.0 * dtsx_chain_latency(ch) / c.fs, c.fs);
    dtsx_chain_destroy(ch);
    static const double f[] = {20, 30, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315,
                               400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150,
                               4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000};
    printf("\nstatic EQ response (dB):\n");
    for (size_t i = 0; i < sizeof(f) / sizeof(f[0]); i++) {
        if (f[i] >= 0.5 * c.fs) break;
        printf("  %6.0f Hz  %+7.2f\n", f[i], dtsx_chain_eq_response_db(&c, c.fs, f[i]));
    }
    return 0;
}

static int cmd_bench(const char *cfgpath)
{
    DtsxConfig c;
    if (load_cfg(&c, cfgpath) != 0) return 2;
    const double secs = 10.0;
    const size_t block = 256;
    const size_t blocks = (size_t)(secs * c.fs / (double)block);
    DtsxChain *ch = dtsx_chain_create(&c, c.fs);
    if (!ch) return 2;
    float l[256], r[256];
    uint32_t s = 0x9e3779b9u;
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (size_t b = 0; b < blocks; b++) {
        for (size_t i = 0; i < block; i++) {
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            const float n = ((float)(s & 0xffff) / 32768.0f - 1.0f) * 0.15f;
            l[i] = n;
            r[i] = -n;
        }
        dtsx_chain_process(ch, l, r, block);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    const double dt = (double)(t1.tv_sec - t0.tv_sec) + 1e-9 * (double)(t1.tv_nsec - t0.tv_nsec);
    printf("processed %.1f s of stereo audio in %.3f s (%.1f x realtime, %.2f%% of one core)\n",
           secs, dt, secs / (dt > 1e-9 ? dt : 1e-9), 100.0 * dt / secs);
    dtsx_chain_destroy(ch);
    return 0;
}

/* --------------------------------------------------------------- selftest */
static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", (msg)); g_fail++; } \
    else printf("ok:   %s\n", (msg)); } while (0)

static int cmd_selftest(void)
{
    printf("dtsdsp selftest\n");
    DtsxConfig c;
    dtsx_config_defaults(&c, 48000.0);
    c.bass_enable = 1; c.bass_amount = 0.5;
    c.eq_enable = 1; c.eq_n = 2;
    c.eq[0] = (DtsxEqBand){DTSX_BQ_PEAK, 100.0, 1.0, 4.0};
    c.eq[1] = (DtsxEqBand){DTSX_BQ_PEAK, 3000.0, 1.0, -3.0};
    c.mbhl_enable = 1;
    c.loud_enable = 1; c.loud_amount = 0.8; c.loud_volume = 0.7;
    c.width = 1.05;
    c.crossfeed = 0.0;
    c.ceiling_db = -1.0;

    for (int k = 0; k < 2; k++) {
        const double fs = k ? 44100.0 : 48000.0;
        DtsxChain *ch = dtsx_chain_create(&c, fs);
        CHECK(ch != NULL, "chain creation");
        if (!ch) continue;
        int finite = 1;
        float peak = 0.0f;
        uint32_t s = 12345u;
        float l[256], r[256];
        for (int b = 0; b < (int)(fs * 2 / 256); b++) {
            for (int i = 0; i < 256; i++) {
                s = s * 1664525u + 1013904223u;
                const float x = ((float)((s >> 8) & 0xffff) / 32768.0f - 1.0f) * 0.6f;
                l[i] = x; r[i] = -x;
            }
            dtsx_chain_process(ch, l, r, 256);
            for (int i = 0; i < 256; i++) {
                if (!isfinite(l[i]) || !isfinite(r[i])) finite = 0;
                const float p = fmaxf(fabsf(l[i]), fabsf(r[i]));
                if (p > peak) peak = p;
            }
        }
        char msg[128];
        snprintf(msg, sizeof msg, "0.6 FS noise stays finite at %g Hz", fs);
        CHECK(finite, msg);
        snprintf(msg, sizeof msg, "peak bounded at %g Hz (%.4f <= %.4f)", fs, peak,
                 pow(10.0, -1.0 / 20.0) + 1e-3);
        CHECK(peak <= pow(10.0, -1.0 / 20.0) + 2e-3, msg);
        dtsx_chain_destroy(ch);
    }

    /* limiter hard test: +20 dB input */
    {
        DtsxChain *ch = dtsx_chain_create(&c, 48000.0);
        int ok = 1;
        float peak = 0.0f;
        float l[256], r[256];
        for (int b = 0; b < 400; b++) {
            for (int i = 0; i < 256; i++) {
                const float x = 0.5f * sinf(2.0f * (float)M_PI * 220.0f *
                                            (float)(b * 256 + i) / 48000.0f) * 10.0f;
                l[i] = x; r[i] = x;
            }
            dtsx_chain_process(ch, l, r, 256);
            for (int i = 0; i < 256; i++) {
                if (!isfinite(l[i])) ok = 0;
                if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
            }
        }
        CHECK(ok, "limiter: +20 dB input stays finite");
        CHECK(peak <= pow(10.0, -1.0 / 20.0) + 2e-3, "limiter: ceiling respected");
        dtsx_chain_destroy(ch);
    }

    /* silence in -> silence out after latency */
    {
        DtsxConfig q;
        dtsx_config_defaults(&q, 48000.0);
        DtsxChain *ch = dtsx_chain_create(&q, 48000.0);
        float l[48000], r[48000];
        memset(l, 0, sizeof l); memset(r, 0, sizeof r);
        dtsx_chain_process(ch, l, r, 48000);
        float peak = 0.0f;
        for (int i = 0; i < 48000; i++) peak = fmaxf(peak, fmaxf(fabsf(l[i]), fabsf(r[i])));
        CHECK(peak == 0.0f, "silence is preserved");
        dtsx_chain_destroy(ch);
    }

    /* bypass is exact */
    {
        DtsxConfig q;
        dtsx_config_defaults(&q, 48000.0);
        q.bypass = 1;
        DtsxChain *ch = dtsx_chain_create(&q, 48000.0);
        float l[64], r[64], l0[64], r0[64];
        for (int i = 0; i < 64; i++) { l[i] = l0[i] = 0.1f * (float)i / 64.0f; r[i] = r0[i] = -l[i]; }
        dtsx_chain_process(ch, l, r, 64);
        int same = 1;
        for (int i = 0; i < 64; i++) if (l[i] != l0[i] || r[i] != r0[i]) same = 0;
        CHECK(same, "bypass is bit-transparent");
        dtsx_chain_destroy(ch);
    }

    printf(g_fail ? "SELFTEST FAILED (%d)\n" : "SELFTEST PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}

int main(int argc, char **argv)
{
    enable_ftz();
    if (argc < 2) {
        fprintf(stderr,
                "usage: %s render|stream|info|bench|selftest [options]\n", argv[0]);
        return 2;
    }
    const char *cfg = NULL;
    const char *in = NULL, *out = NULL;
    double rate = 0.0;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "-p") && i + 1 < argc) cfg = argv[++i];
        else if (!strcmp(argv[i], "-i") && i + 1 < argc) in = argv[++i];
        else if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atof(argv[++i]);
    }
    if (!strcmp(argv[1], "render")) {
        if (!in || !out) { fprintf(stderr, "render needs -i and -o\n"); return 2; }
        return cmd_render(cfg, in, out);
    }
    if (!strcmp(argv[1], "stream")) return cmd_stream(cfg, rate);
    if (!strcmp(argv[1], "info")) return cmd_info(cfg);
    if (!strcmp(argv[1], "bench")) return cmd_bench(cfg);
    if (!strcmp(argv[1], "selftest")) return cmd_selftest();
    fprintf(stderr, "unknown command %s\n", argv[1]);
    return 2;
}
