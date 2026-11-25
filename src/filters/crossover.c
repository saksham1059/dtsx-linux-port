/* Linkwitz-Riley 4th-order crossover and shared helpers. */
#include "../dtsx_internal.h"

void lr4_init(Lr4 *x, double fs, double f)
{
    dtsx_bq_design(&x->l1, DTSX_BQ_LPF, fs, f, 0.7071067811865476, 0.0);
    dtsx_bq_design(&x->l2, DTSX_BQ_LPF, fs, f, 0.7071067811865476, 0.0);
    dtsx_bq_design(&x->h1, DTSX_BQ_HPF, fs, f, 0.7071067811865476, 0.0);
    dtsx_bq_design(&x->h2, DTSX_BQ_HPF, fs, f, 0.7071067811865476, 0.0);
}

void lr4_reset(Lr4 *x)
{
    dtsx_bq_zero(&x->l1); dtsx_bq_zero(&x->l2);
    dtsx_bq_zero(&x->h1); dtsx_bq_zero(&x->h2);
}

void lr4_split(Lr4 *x, float in, float *lo, float *hi)
{
    const float a = dtsx_bq_tick(&x->l1, in);
    const float b = dtsx_bq_tick(&x->l2, a);
    const float c = dtsx_bq_tick(&x->h1, in);
    const float d = dtsx_bq_tick(&x->h2, c);
    *lo = b;
    *hi = d;
}

float dtsx_soft_limit(float x, float ceiling)
{
    if (ceiling <= 0.0f) ceiling = 1.0f;
    const float a = fabsf(x);
    const float knee = 0.72f * ceiling;
    if (a <= knee) return x;
    const float span = ceiling - knee;
    const float y = knee + span * tanhf((a - knee) / span);
    return (x < 0.0f) ? -y : y;
}
