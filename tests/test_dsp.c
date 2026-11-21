/* Automated DSP tests. Run with `make test`. */
#include "../src/dtsx_internal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static int g_fail;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); g_fail++; } \
    else { printf("ok:   "); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double rms(const float *x, size_t n)
{
    double s = 0.0;
    for (size_t i = 0; i < n; i++) s += (double)x[i] * x[i];
    return sqrt(s / (double)n);
}

static void test_biquads(void)
{
    DtsxBiquad q;
    dtsx_bq_design(&q, DTSX_BQ_PEAK, 48000, 1000, 1.0, 6.0);
    const double g = dtsx_biquad_response_db(&q, 48000, 1000);
    CHECK(fabs(g - 6.0) < 0.35, "peaking +6 dB at 1 kHz measures %.2f dB", g);

    dtsx_bq_design(&q, DTSX_BQ_HPF, 48000, 100, 0.70710678, 0.0);
    const double a = dtsx_biquad_response_db(&q, 48000, 20);
    const double b = dtsx_biquad_response_db(&q, 48000, 5000);
    CHECK(a < -25.0, "HPF at 100 Hz: 20 Hz is %.1f dB", a);
    CHECK(fabs(b) < 0.2, "HPF at 100 Hz: 5 kHz is %.2f dB", b);

    /* long-run numerical stability with a resonant boost */
    dtsx_bq_design(&q, DTSX_BQ_PEAK, 48000, 40, 8.0, 12.0);
    float peak = 0.0f;
    int finite = 1;
    for (int i = 0; i < 48000 * 5; i++) {
        const float x = (i % 4800 < 2400) ? 0.5f : -0.5f;
        const float y = dtsx_bq_tick(&q, x);
        if (!isfinite(y)) finite = 0;
        if (fabsf(y) > peak) peak = fabsf(y);
    }
    CHECK(finite && peak < 100.0f, "resonant biquad stable (peak %.2f)", peak);
}

static void test_crossover(void)
{
    Lr4 x;
    lr4_init(&x, 48000, 440.0);
    float in[20000], lo[20000], hi[20000];
    for (int i = 0; i < 20000; i++)
        in[i] = 0.4f * sinf(2.0f * (float)M_PI * 200.0f * (float)i / 48000.0f);
    for (int i = 0; i < 20000; i++) lr4_split(&x, in[i], &lo[i], &hi[i]);
    float sum[10000];
    for (int i = 0; i < 10000; i++) sum[i] = lo[i + 10000] + hi[i + 10000];
    const double ratio = rms(sum, 10000) / rms(in + 10000, 10000);
    CHECK(fabs(ratio - 1.0) < 0.02, "LR4 low+high reconstructs (ratio %.4f)", ratio);
}

static void test_limiter(void)
{
    DtsxConfig c;
    dtsx_config_defaults(&c, 48000);
    c.ceiling_db = -1.0;
    c.lookahead_ms = 5.0;
    Limiter li;
    CHECK(limiter_init(&li, &c, 48000) == 0, "limiter init");
    float l[256], r[256];
    float peak = 0.0f;
    int finite = 1;
    for (int b = 0; b < 400; b++) {
        for (int i = 0; i < 256; i++) {
            const float v = 0.6f * sinf(2.0f * (float)M_PI * 400.0f *
                                        (float)(b * 256 + i) / 48000.0f);
            l[i] = r[i] = (b > 100) ? v * 8.0f : v;
        }
        limiter_process(&li, l, r, 256);
        for (int i = 0; i < 256; i++) {
            if (!isfinite(l[i])) finite = 0;
            if (fabsf(l[i]) > peak) peak = fabsf(l[i]);
        }
    }
    CHECK(finite, "limiter output finite");
    CHECK(peak <= pow(10.0, -1.0 / 20.0) + 2e-3,
          "limiter ceiling respected (peak %.4f)", peak);
    CHECK(li.max_gain_reduction_db > 6.0,
          "limiter applied gain reduction (%.1f dB)", li.max_gain_reduction_db);
    limiter_free(&li);
}

static void test_chain_rate(double fs)
{
    DtsxConfig c;
    dtsx_config_defaults(&c, fs);
    c.hpf_enable = 1; c.hpf_hz = 100;
    c.eq_enable = 1; c.eq_n = 3;
    c.eq[0] = (DtsxEqBand){DTSX_BQ_LOWSHELF, 120, 0.7, 4.0};
    c.eq[1] = (DtsxEqBand){DTSX_BQ_PEAK, 900, 1.2, -3.0};
    c.eq[2] = (DtsxEqBand){DTSX_BQ_HIGHSHELF, 8000, 0.7, 2.0};
    c.bass_enable = 1; c.bass_amount = 0.7; c.mbhl_enable = 1;
    c.loud_enable = 1; c.loud_amount = 1.0; c.loud_volume = 1.0;
    c.width = 1.1;
    c.ceiling_db = -1.0;

    DtsxChain *ch = dtsx_chain_create(&c, fs);
    CHECK(ch != NULL, "chain created at %.0f Hz", fs);
    if (!ch) return;
    const size_t n = 256;
    float l[256], r[256];
    float peak = 0.0f;
    int finite = 1;
    unsigned s = 7u;
    for (int b = 0; b < (int)(fs * 3 / 256); b++) {
        for (size_t i = 0; i < n; i++) {
            s = s * 1664525u + 1013904223u;
            const float x = ((float)((s >> 8) & 0xffff) / 32768.0f - 1.0f) * 0.8f;
            l[i] = x;
            r[i] = -0.7f * x;
        }
        dtsx_chain_process(ch, l, r, n);
        for (size_t i = 0; i < n; i++) {
            if (!isfinite(l[i]) || !isfinite(r[i])) finite = 0;
            const float p = fmaxf(fabsf(l[i]), fabsf(r[i]));
            if (p > peak) peak = p;
        }
    }
    CHECK(finite, "chain finite at %.0f Hz", fs);
    CHECK(peak <= pow(10.0, -1.0 / 20.0) + 3e-3,
          "chain peak bounded at %.0f Hz (%.4f)", fs, peak);
    CHECK(dtsx_chain_latency(ch) > 0, "latency reported (%u samples)",
          dtsx_chain_latency(ch));
    dtsx_chain_destroy(ch);
}

static void test_bypass_control_is_idempotent(void)
{
    DtsxConfig c;
    dtsx_config_defaults(&c, 48000);
    c.hpf_enable = 1;
    c.hpf_hz = 100.0;
    DtsxChain *ch = dtsx_chain_create(&c, 48000);
    CHECK(ch != NULL, "bypass-idempotence chain created");
    if (!ch) return;

    float l[256], r[256];
    float prev = 0.0f, max_step = 0.0f;
    double energy = 0.0;
    unsigned n = 0;
    for (int b = 0; b < 200; b++) {
        /* the LADSPA host calls this once per run() with the same value */
        dtsx_chain_set_bypass(ch, 0);
        for (int i = 0; i < 256; i++) {
            const float x = 0.25f * sinf(2.0f * (float)M_PI * 1000.0f *
                                         (float)(b * 256 + i) / 48000.0f);
            l[i] = x; r[i] = x;
        }
        dtsx_chain_process(ch, l, r, 256);
        for (int i = 0; i < 256; i++) {
            if (b > 20) {
                const float d = fabsf(l[i] - prev);
                if (d > max_step) max_step = d;
                energy += (double)l[i] * l[i];
                n++;
            }
            prev = l[i];
        }
    }
    const double rms = sqrt(energy / (double)(n ? n : 1));
    CHECK(max_step < 0.06f,
          "repeated bypass-off keeps filter state (max step %.4f)", max_step);
    CHECK(rms > 0.15, "signal passes while bypass-off is set (rms %.3f)", rms);
    dtsx_chain_destroy(ch);
}

static void test_config_roundtrip(void)
{
    DtsxConfig a, b;
    dtsx_config_defaults(&a, 44100);
    a.eq_enable = 1;
    a.eq_n = 2;
    a.eq[0] = (DtsxEqBand){DTSX_BQ_PEAK, 250, 1.4, 3.5};
    a.eq[1] = (DtsxEqBand){DTSX_BQ_HIGHSHELF, 9000, 0.7, -2.0};
    a.bass_amount = 0.42;
    a.width = 1.2;
    CHECK(dtsx_config_write(&a, "build/test_profile.json") == 0, "config write");
    dtsx_config_defaults(&b, 48000);
    CHECK(dtsx_config_load(&b, "build/test_profile.json") == 0, "config read");
    CHECK(b.eq_n == 2 && fabs(b.eq[0].f - 250.0) < 1e-6 &&
          fabs(b.eq[0].gain - 3.5) < 1e-6, "config round-trips EQ");
    CHECK(fabs(b.bass_amount - 0.42) < 1e-6, "config round-trips bass");
    CHECK(fabs(b.width - 1.2) < 1e-6, "config round-trips width");
}

int main(void)
{
    printf("dtsx DSP tests\n");
    test_biquads();
    test_crossover();
    test_limiter();
    test_chain_rate(44100);
    test_chain_rate(48000);
    test_chain_rate(96000);
    test_bypass_control_is_idempotent();
    test_config_roundtrip();
    printf(g_fail ? "TESTS FAILED (%d)\n" : "ALL TESTS PASSED\n", g_fail);
    return g_fail ? 1 : 0;
}
