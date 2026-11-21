/* MBHL-inspired three-band dynamics (multiband compression / loudness).
 *
 * The OEM profile exposes a three-band processor with independent attack,
 * decay, ratio, threshold and makeup per band, plus a boost. This module
 * implements that class of processor independently: Linkwitz-Riley 4th-order
 * band splitting, envelope-following gain computer per band, smoothed gain
 * reduction, per-band makeup, and a low-band boost trim.
 */
#include "../dtsx_internal.h"

static void comp_init(Comp *c, double fs, double thresh_db, double ratio,
                      double atk_ms, double rel_ms, double makeup_db)
{
    c->env = 0.0f;
    c->gr_db = 0.0f;
    c->atk = (float)(1.0 - exp(-1.0 / (fs * atk_ms / 1000.0 + 1e-9)));
    c->rel = (float)(1.0 - exp(-1.0 / (fs * rel_ms / 1000.0 + 1e-9)));
    c->thresh_db = (float)thresh_db;
    c->ratio = ratio > 1.0 ? (float)ratio : 1.0f;
    c->makeup = (float)DTSX_DB2LIN(makeup_db);
}

static inline float comp_tick(Comp *c, float x)
{
    const float a = fabsf(x);
    const float coef = (a > c->env) ? c->atk : c->rel;
    c->env += coef * (a - c->env);

    const float lev_db = (float)DTSX_LIN2DB(c->env);
    float over = lev_db - c->thresh_db;
    if (over < 0.0f) over = 0.0f;
    const float want = over * (1.0f - 1.0f / c->ratio);
    const float gc = (want > c->gr_db) ? c->atk : c->rel;
    c->gr_db += gc * (want - c->gr_db);
    if (c->gr_db < 1e-4f) c->gr_db = 0.0f;

    const float gain = expf(-c->gr_db * 0.1151292546f); /* 10^(-gr/20) */
    return x * gain * c->makeup;
}

void mbhl_init(Mbhl *m, const DtsxConfig *c, double fs)
{
    m->enabled = c->mbhl_enable;
    m->fs = fs;
    const double lo_x = c->mbhl_low_x > 40.0 ? c->mbhl_low_x : 440.0;
    const double mid_x = c->mbhl_mid_x > lo_x ? c->mbhl_mid_x : 2000.0;
    m->boost_lin = (float)DTSX_DB2LIN(c->mbhl_boost_db);
    m->mix = 1.0f;

    for (int i = 0; i < 2; i++) {
        MbhlChannel *b = &m->ch[i];
        lr4_init(&b->split_lo, fs, lo_x);
        lr4_init(&b->split_mid, fs, mid_x);
        comp_init(&b->lo, fs, c->mbhl_low_thresh_db, c->mbhl_low_ratio,
                  c->mbhl_attack_ms, c->mbhl_low_decay_ms, c->mbhl_low_makeup_db);
        comp_init(&b->mid, fs, c->mbhl_mid_thresh_db, c->mbhl_mid_ratio,
                  c->mbhl_attack_ms, c->mbhl_mid_decay_ms, c->mbhl_mid_makeup_db);
        comp_init(&b->hi, fs, c->mbhl_high_thresh_db, c->mbhl_high_ratio,
                  c->mbhl_attack_ms, c->mbhl_high_decay_ms, c->mbhl_high_makeup_db);
    }
}

void mbhl_reset(Mbhl *m)
{
    for (int i = 0; i < 2; i++) {
        lr4_reset(&m->ch[i].split_lo);
        lr4_reset(&m->ch[i].split_mid);
        m->ch[i].lo.env = m->ch[i].lo.gr_db = 0.0f;
        m->ch[i].mid.env = m->ch[i].mid.gr_db = 0.0f;
        m->ch[i].hi.env = m->ch[i].hi.gr_db = 0.0f;
    }
}

void mbhl_set_amount(Mbhl *m, double amount01)
{
    m->mix = (float)(amount01 < 0.0 ? 0.0 : (amount01 > 1.0 ? 1.0 : amount01));
}

static inline float band_process(Mbhl *m, MbhlChannel *b, float x)
{
    float lo, hi1, mid, hi;
    lr4_split(&b->split_lo, x, &lo, &hi1);
    lr4_split(&b->split_mid, hi1, &mid, &hi);
    lo = comp_tick(&b->lo, lo) * m->boost_lin;
    mid = comp_tick(&b->mid, mid);
    hi = comp_tick(&b->hi, hi);
    return lo + mid + hi;
}

void mbhl_process(Mbhl *m, float *l, float *r, size_t n)
{
    if (!m->enabled) return;
    if (m->mix < 1e-4f) return;
    for (size_t i = 0; i < n; i++) {
        const float xl = l[i], xr = r[i];
        const float yl = band_process(m, &m->ch[0], xl);
        const float yr = band_process(m, &m->ch[1], xr);
        l[i] = xl + m->mix * (yl - xl);
        r[i] = xr + m->mix * (yr - xr);
    }
}
