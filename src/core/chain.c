/* Processing chain assembly and the real-time entry point. */
#include "dtsx_internal.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct DtsxChain {
    DtsxConfig cfg;
    double fs;
    int bypass;

    DtsxBiquad hpf[2][4];
    int hpf_stages;

    DtsxBiquad eq[2][DTSX_MAX_BANDS];
    int eq_n;

    DtsxBiquad user_eq[2][DTSX_USER_EQ_BANDS];
    int user_eq_n;

    TruBass bass[2];
    Mbhl mbhl;
    Loudness loud;
    Spatial sp;
    Limiter lim;

    Sm in_gain, out_gain;
};

static void setup_hpf(DtsxChain *ch, double fs, double f, int order)
{
    /* order 2: one biquad (Butterworth); order 4: two cascaded biquads */
    ch->hpf_stages = (order >= 4) ? 2 : 1;
    const double q1 = (order >= 4) ? 0.541196100146197 : 0.7071067811865476;
    const double q2 = 1.306562964876377;
    for (int c = 0; c < 2; c++) {
        dtsx_bq_design(&ch->hpf[c][0], DTSX_BQ_HPF, fs, f, q1, 0.0);
        if (ch->hpf_stages == 2)
            dtsx_bq_design(&ch->hpf[c][1], DTSX_BQ_HPF, fs, f, q2, 0.0);
        for (int i = ch->hpf_stages; i < 4; i++)
            dtsx_bq_design(&ch->hpf[c][i], DTSX_BQ_HPF, fs, f, 0.7071, 0.0);
    }
}

DtsxChain *dtsx_chain_create(const DtsxConfig *cfg, double fs)
{
    DtsxChain *ch = (DtsxChain *)calloc(1, sizeof(*ch));
    if (!ch) return NULL;
    ch->cfg = *cfg;
    if (fs <= 0.0) fs = cfg->fs > 0.0 ? cfg->fs : 48000.0;
    ch->fs = fs;
    ch->bypass = cfg->bypass;

    setup_hpf(ch, fs, cfg->hpf_hz > 5.0 ? cfg->hpf_hz : 100.0,
              cfg->hpf_order >= 4 ? 4 : 2);
    if (!cfg->hpf_enable) ch->hpf_stages = 0;

    ch->eq_n = cfg->eq_enable ? (cfg->eq_n < DTSX_MAX_BANDS ? cfg->eq_n : DTSX_MAX_BANDS) : 0;
    for (int i = 0; i < ch->eq_n; i++) {
        const DtsxEqBand *b = &cfg->eq[i];
        DtsxBiquadType t = (DtsxBiquadType)b->type;
        for (int c = 0; c < 2; c++)
            dtsx_bq_design(&ch->eq[c][i], t, fs, b->f, b->q, b->gain);
    }

    ch->user_eq_n = cfg->user_eq_enable ? DTSX_USER_EQ_BANDS : 0;
    for (int i = 0; i < ch->user_eq_n; i++) {
        for (int c = 0; c < 2; c++)
            dtsx_bq_design(&ch->user_eq[c][i], DTSX_BQ_PEAK, fs,
                            dtsx_user_eq_freqs[i], 1.0, cfg->user_eq_g[i]);
    }

    for (int c = 0; c < 2; c++)
        truhdx_init(&ch->bass[c], &ch->cfg, fs);

    mbhl_init(&ch->mbhl, &ch->cfg, fs);
    loudness_init(&ch->loud, &ch->cfg, fs);
    if (spatial_init(&ch->sp, &ch->cfg, fs) != 0) {
        free(ch);
        return NULL;
    }
    if (limiter_init(&ch->lim, &ch->cfg, fs) != 0) {
        spatial_free(&ch->sp);
        free(ch);
        return NULL;
    }

    sm_init(&ch->in_gain, (float)DTSX_DB2LIN(cfg->input_gain_db), fs, 40.0);
    sm_init(&ch->out_gain, (float)DTSX_DB2LIN(cfg->output_gain_db), fs, 40.0);
    return ch;
}

void dtsx_chain_destroy(DtsxChain *ch)
{
    if (!ch) return;
    spatial_free(&ch->sp);
    limiter_free(&ch->lim);
    free(ch);
}

void dtsx_chain_reset(DtsxChain *ch)
{
    if (!ch) return;
    for (int c = 0; c < 2; c++) {
        for (int i = 0; i < 4; i++) dtsx_bq_zero(&ch->hpf[c][i]);
        for (int i = 0; i < ch->eq_n; i++) dtsx_bq_zero(&ch->eq[c][i]);
        truhdx_reset(&ch->bass[c]);
    }
    mbhl_reset(&ch->mbhl);
    loudness_reset(&ch->loud);
    spatial_reset(&ch->sp);
    limiter_reset(&ch->lim);
}

void dtsx_chain_process(DtsxChain *ch, float *l, float *r, size_t n)
{
    if (!ch || n == 0) return;
    if (ch->bypass) return;

    for (size_t i = 0; i < n; i++) {
        const float gi = sm_tick(&ch->in_gain);
        float a = l[i] * gi, b = r[i] * gi;

        /* Evidence-based order: SFX (TruBass) -> GPEQ -> EFX (HPF). */
        if (ch->bass[0].enabled) {
            a = truhdx_tick(&ch->bass[0], a);
            b = truhdx_tick(&ch->bass[1], b);
        }
        for (int k = 0; k < ch->eq_n; k++) {
            a = dtsx_bq_tick(&ch->eq[0][k], a);
            b = dtsx_bq_tick(&ch->eq[1][k], b);
        }
        for (int k = 0; k < ch->user_eq_n; k++) {
            a = dtsx_bq_tick(&ch->user_eq[0][k], a);
            b = dtsx_bq_tick(&ch->user_eq[1][k], b);
        }
        for (int s = 0; s < ch->hpf_stages; s++) {
            a = dtsx_bq_tick(&ch->hpf[0][s], a);
            b = dtsx_bq_tick(&ch->hpf[1][s], b);
        }
        l[i] = a;
        r[i] = b;
    }

    mbhl_process(&ch->mbhl, l, r, n);
    loudness_process(&ch->loud, l, r, n);
    spatial_process(&ch->sp, l, r, n);

    for (size_t i = 0; i < n; i++) {
        const float go = sm_tick(&ch->out_gain);
        l[i] *= go;
        r[i] *= go;
    }
    limiter_process(&ch->lim, l, r, n);
}

static void redesign_keep_state(DtsxBiquad *q, DtsxBiquadType type, double fs,
                                double f, double qq, double gain)
{
    const float z1 = q->z1, z2 = q->z2;
    dtsx_bq_design(q, type, fs, f, qq, gain);
    q->z1 = z1; q->z2 = z2;
}

unsigned dtsx_chain_live_apply(DtsxChain *ch, const DtsxConfig *cfg)
{
    if (!ch || !cfg) return 0;
    const double fs = ch->fs;
    unsigned result = 0;

    dtsx_chain_set_bypass(ch, cfg->bypass);
    sm_set(&ch->in_gain, (float)DTSX_DB2LIN(cfg->input_gain_db));
    sm_set(&ch->out_gain, (float)DTSX_DB2LIN(cfg->output_gain_db));

    ch->hpf_stages = cfg->hpf_enable ? (cfg->hpf_order >= 4 ? 2 : 1) : 0;
    if (ch->hpf_stages) {
        const double q1 = cfg->hpf_order >= 4 ? 0.541196100146197 : 0.7071067811865476;
        const double q2 = 1.306562964876377;
        for (int c = 0; c < 2; c++) {
            redesign_keep_state(&ch->hpf[c][0], DTSX_BQ_HPF, fs, cfg->hpf_hz, q1, 0.0);
            if (ch->hpf_stages == 2)
                redesign_keep_state(&ch->hpf[c][1], DTSX_BQ_HPF, fs, cfg->hpf_hz, q2, 0.0);
        }
    }

    ch->eq_n = cfg->eq_enable ? cfg->eq_n : 0;
    if (ch->eq_n > DTSX_MAX_BANDS) ch->eq_n = DTSX_MAX_BANDS;
    for (int i = 0; i < ch->eq_n; i++)
        for (int c = 0; c < 2; c++)
            redesign_keep_state(&ch->eq[c][i], (DtsxBiquadType)cfg->eq[i].type, fs,
                                cfg->eq[i].f, cfg->eq[i].q, cfg->eq[i].gain);
    ch->user_eq_n = cfg->user_eq_enable ? DTSX_USER_EQ_BANDS : 0;
    for (int i = 0; i < ch->user_eq_n; i++)
        for (int c = 0; c < 2; c++)
            redesign_keep_state(&ch->user_eq[c][i], DTSX_BQ_PEAK, fs,
                                dtsx_user_eq_freqs[i], 1.0, cfg->user_eq_g[i]);

    /* Bass gains are smoothed. Coefficients are updated in-place and retain
     * their delay state, so this work stays bounded and allocation-free. */
    for (int c = 0; c < 2; c++) {
        TruBass *b = &ch->bass[c];
        const double lo = cfg->bass_lo_hz;
        b->enabled = cfg->bass_enable ? 1 : 0;
        sm_set(&b->amount, (float)cfg->bass_amount);
        sm_set(&b->drive, (float)cfg->bass_drive);
        sm_set(&b->mix, (float)cfg->bass_harm_mix);
        redesign_keep_state(&b->lp, DTSX_BQ_LPF, fs, lo, 0.7071067811865476, 0.0);
        redesign_keep_state(&b->hp, DTSX_BQ_HPF, fs, cfg->bass_harm_hp_hz, 0.7071067811865476, 0.0);
        redesign_keep_state(&b->shaper, DTSX_BQ_LPF, fs, lo * 3.5, 0.8, 0.0);
        redesign_keep_state(&b->ext, DTSX_BQ_LOWSHELF, fs, lo * 0.8, 0.7071067811865476, cfg->bass_extend_db);
        redesign_keep_state(&b->size_hp, DTSX_BQ_HPF, fs, cfg->bass_size_hz, 0.7071067811865476, 0.0);
    }
    /* mbhl_init is bounded and only updates fixed in-place state. It performs
     * no allocation, locking, I/O, or logging. */
    mbhl_init(&ch->mbhl, cfg, fs);
    ch->loud.enabled = cfg->loud_enable ? 1 : 0;
    dtsx_chain_set_loudness(ch, cfg->loud_amount * cfg->loud_volume);
    dtsx_chain_set_width(ch, cfg->width);
    sm_set(&ch->sp.xf, (float)cfg->crossfeed);
    ch->lim.ceiling = (float)DTSX_DB2LIN(cfg->ceiling_db);
    if (ch->lim.ceiling > 1.0f) ch->lim.ceiling = 1.0f;
    if (ch->lim.ceiling < 1e-3f) ch->lim.ceiling = 1e-3f;
    ch->cfg = *cfg;
    result |= DTSX_LIVE_APPLIED;
    return result;
}

void dtsx_chain_set_bass(DtsxChain *ch, double a)
{
    if (!ch) return;
    if (a < 0.0) a = 0.0;
    if (a > 1.0) a = 1.0;
    for (int c = 0; c < 2; c++) sm_set(&ch->bass[c].amount, (float)a);
}

void dtsx_chain_set_loudness(DtsxChain *ch, double a)
{
    if (!ch) return;
    loudness_set(&ch->loud, a);
}

void dtsx_chain_set_width(DtsxChain *ch, double w)
{
    if (!ch) return;
    spatial_set_width(&ch->sp, w);
}

void dtsx_chain_set_bypass(DtsxChain *ch, int bypass)
{
    if (!ch) return;
    const int was = ch->bypass;
    ch->bypass = bypass ? 1 : 0;
    /* Reset only on an actual bypass -> active transition. Resetting on every
     * call would clear filter state once per audio block. */
    if (was && !ch->bypass) dtsx_chain_reset(ch);
}

unsigned dtsx_chain_latency(const DtsxChain *ch)
{
    return ch ? (unsigned)ch->lim.n : 0u;
}

double dtsx_biquad_response_db(const DtsxBiquad *q, double fs, double hz)
{
    const double w = 2.0 * M_PI * hz / fs;
    const double c1 = cos(w), s1 = sin(w);
    const double c2 = cos(2 * w), s2 = sin(2 * w);
    const double nr = q->b0 + q->b1 * c1 + q->b2 * c2;
    const double ni = -(q->b1 * s1 + q->b2 * s2);
    const double dr = 1.0 + q->a1 * c1 + q->a2 * c2;
    const double di = -(q->a1 * s1 + q->a2 * s2);
    const double num = nr * nr + ni * ni;
    const double den = dr * dr + di * di;
    return 10.0 * log10((num + 1e-30) / (den + 1e-30));
}

double dtsx_chain_eq_response_db(const DtsxConfig *cfg, double fs, double hz)
{
    double total = cfg->input_gain_db + cfg->output_gain_db;
    if (cfg->hpf_enable) {
        DtsxBiquad q;
        const int order = cfg->hpf_order >= 4 ? 4 : 2;
        const double q1 = (order == 4) ? 0.541196100146197 : 0.7071067811865476;
        const double q2 = 1.306562964876377;
        dtsx_bq_design(&q, DTSX_BQ_HPF, fs, cfg->hpf_hz, q1, 0.0);
        total += dtsx_biquad_response_db(&q, fs, hz);
        if (order == 4) {
            dtsx_bq_design(&q, DTSX_BQ_HPF, fs, cfg->hpf_hz, q2, 0.0);
            total += dtsx_biquad_response_db(&q, fs, hz);
        }
    }
    if (cfg->eq_enable) {
        for (int i = 0; i < cfg->eq_n && i < DTSX_MAX_BANDS; i++) {
            DtsxBiquad q;
            const DtsxEqBand *b = &cfg->eq[i];
            dtsx_bq_design(&q, (DtsxBiquadType)b->type, fs, b->f, b->q, b->gain);
            total += dtsx_biquad_response_db(&q, fs, hz);
        }
    }
    if (cfg->user_eq_enable) {
        for (int i = 0; i < DTSX_USER_EQ_BANDS; i++) {
            DtsxBiquad q;
            dtsx_bq_design(&q, DTSX_BQ_PEAK, fs, dtsx_user_eq_freqs[i], 1.0,
                            cfg->user_eq_g[i]);
            total += dtsx_biquad_response_db(&q, fs, hz);
        }
    }
    return total;
}
