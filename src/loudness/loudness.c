/* Volume-dependent loudness compensation.
 *
 * Implemented as low/high shelving gain that follows a smoothed control value.
 * Coefficients are refreshed at control rate (every 64 samples) and the filter
 * state is preserved, so no zipper noise or allocation occurs.
 */
#include "../dtsx_internal.h"

#define LOUD_CTRL_INTERVAL 64

static void loud_setup(Loudness *l, float amount)
{
    /* up to +8 dB low shelf and +3.5 dB high shelf at full compensation */
    const double g_lo = 8.0 * (double)amount;
    const double g_hi = 3.5 * (double)amount;
    /* preserve state across coefficient updates */
    float l1 = l->lo.z1, l2 = l->lo.z2, h1 = l->hi.z1, h2 = l->hi.z2;
    float l1b = l->lo2.z1, l2b = l->lo2.z2, h1b = l->hi2.z1, h2b = l->hi2.z2;
    dtsx_bq_design(&l->lo, DTSX_BQ_LOWSHELF, l->fs, 110.0, 0.7, g_lo);
    dtsx_bq_design(&l->hi, DTSX_BQ_HIGHSHELF, l->fs, 6500.0, 0.7, g_hi);
    dtsx_bq_design(&l->lo2, DTSX_BQ_LOWSHELF, l->fs, 110.0, 0.7, g_lo);
    dtsx_bq_design(&l->hi2, DTSX_BQ_HIGHSHELF, l->fs, 6500.0, 0.7, g_hi);
    l->lo.z1 = l1; l->lo.z2 = l2; l->hi.z1 = h1; l->hi.z2 = h2;
    l->lo2.z1 = l1b; l->lo2.z2 = l2b; l->hi2.z1 = h1b; l->hi2.z2 = h2b;
}

void loudness_init(Loudness *l, const DtsxConfig *c, double fs)
{
    l->enabled = c->loud_enable && c->loud_amount > 0.0001;
    l->fs = fs;
    sm_init(&l->amount, (float)(c->loud_amount * c->loud_volume), fs, 150.0);
    l->ctr = 0;
    l->last_gain = -1.0f;
    l->lo.z1 = l->lo.z2 = l->hi.z1 = l->hi.z2 = 0.0f;
    l->lo2.z1 = l->lo2.z2 = l->hi2.z1 = l->hi2.z2 = 0.0f;
    loud_setup(l, l->amount.cur);
}

void loudness_reset(Loudness *l)
{
    dtsx_bq_zero(&l->lo); dtsx_bq_zero(&l->hi);
    dtsx_bq_zero(&l->lo2); dtsx_bq_zero(&l->hi2);
    l->ctr = 0;
}

void loudness_set(Loudness *l, double amount01)
{
    if (amount01 < 0.0) amount01 = 0.0;
    if (amount01 > 1.0) amount01 = 1.0;
    sm_set(&l->amount, (float)amount01);
}

void loudness_process(Loudness *l, float *lch, float *rch, size_t n)
{
    if (!l->enabled) return;
    for (size_t i = 0; i < n; i++) {
        if ((l->ctr++ & (LOUD_CTRL_INTERVAL - 1)) == 0) {
            const float a = sm_tick(&l->amount);
            if (fabsf(a - l->last_gain) > 0.002f) {
                loud_setup(l, a);
                l->last_gain = a;
            }
        }
        float x = lch[i];
        x = dtsx_bq_tick(&l->lo, x);
        x = dtsx_bq_tick(&l->hi, x);
        lch[i] = x;
        x = rch[i];
        x = dtsx_bq_tick(&l->lo2, x);
        x = dtsx_bq_tick(&l->hi2, x);
        rch[i] = x;
    }
}
