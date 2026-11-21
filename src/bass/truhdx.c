/* TruBass HDX-inspired harmonic bass enhancement.
 *
 * Independent implementation of the well-documented psychoacoustic bass
 * technique: extract the low band, rectify it to create even/odd harmonics,
 * remove the DC component, band-limit the harmonic product so it stays in a
 * range small speakers can reproduce, and mix it under the original signal.
 * The Windows module is a black box; this is *not* a bit-exact clone.
 *
 * Safety: the synthesised harmonics are soft-saturated, the dry low band is
 * high-passed at the configured speaker-size frequency, and the final output
 * stage limits to the configured ceiling.
 */
#include "../dtsx_internal.h"

void truhdx_init(TruBass *t, const DtsxConfig *c, double fs)
{
    t->enabled = c->bass_enable && c->bass_amount > 0.0001;
    t->fs = fs;
    sm_init(&t->amount, (float)c->bass_amount, fs, 60.0);
    sm_init(&t->drive, (float)c->bass_drive, fs, 60.0);
    sm_init(&t->mix, (float)(c->bass_harm_mix < 0.0 ? 0.0 :
                            (c->bass_harm_mix > 1.0 ? 1.0 : c->bass_harm_mix)),
            fs, 60.0);

    double lo = c->bass_lo_hz > 20.0 ? c->bass_lo_hz : 150.0;
    double hh = c->bass_harm_hp_hz > 10.0 ? c->bass_harm_hp_hz : 55.0;
    double sz = c->bass_size_hz > 20.0 ? c->bass_size_hz : 55.0;
    if (lo > 0.4 * fs) lo = 0.4 * fs;
    if (sz > lo) sz = lo;

    dtsx_bq_design(&t->lp, DTSX_BQ_LPF, fs, lo, 0.7071067811865476, 0.0);
    dtsx_bq_design(&t->hp, DTSX_BQ_HPF, fs, hh, 0.7071067811865476, 0.0);
    /* keep synthesised harmonics below ~500 Hz, where small drivers work */
    dtsx_bq_design(&t->shaper, DTSX_BQ_LPF, fs,
                   (lo * 3.5 > 0.35 * fs) ? 0.35 * fs : lo * 3.5, 0.8, 0.0);
    dtsx_bq_design(&t->ext, DTSX_BQ_LOWSHELF, fs,
                   (lo * 0.8 < 120.0) ? lo * 0.8 : 120.0, 0.7071067811865476,
                   c->bass_extend_db);
    dtsx_bq_design(&t->size_hp, DTSX_BQ_HPF, fs, sz, 0.7071067811865476, 0.0);

    t->dc = 0.0f;
    t->dc_coef = (float)(1.0 - exp(-2.0 * M_PI * 12.0 / fs));
}

void truhdx_reset(TruBass *t)
{
    dtsx_bq_zero(&t->lp); dtsx_bq_zero(&t->hp);
    dtsx_bq_zero(&t->shaper); dtsx_bq_zero(&t->ext);
    dtsx_bq_zero(&t->size_hp);
    t->dc = 0.0f;
}

float truhdx_tick(TruBass *t, float x)
{
    const float amt = sm_tick(&t->amount);
    const float drv = sm_tick(&t->drive);
    const float mix = sm_tick(&t->mix);
    if (amt < 1e-4f) return x;

    const float low = dtsx_bq_tick(&t->lp, x);
    float rect = fabsf(low) * drv;
    t->dc += t->dc_coef * (rect - t->dc);
    rect -= t->dc;

    float h = dtsx_bq_tick(&t->hp, rect);
    h = dtsx_bq_tick(&t->shaper, h);
    /* gentle saturation controls harmonic runaway */
    h = h / (1.0f + fabsf(h) * 0.7f);

    /* dry low band, protected below the driver's practical range */
    float dry = dtsx_bq_tick(&t->size_hp, low);
    dry = dtsx_bq_tick(&t->ext, dry);

    return x + amt * (0.85f * dry + 0.55f * mix * h);
}
