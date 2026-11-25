/* Stable RBJ biquad design and transposed-direct-form-II processing.
 *
 * References: R. Bristow-Johnson, "Cookbook formulae for audio EQ biquad
 * filter coefficients" (public reference); A. Oppenheim & R. Schafer,
 * "Discrete-Time Signal Processing" (TDF-II numerics).
 */
#include "dtsx.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static double clampf(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void dtsx_bq_design(DtsxBiquad *q, DtsxBiquadType type, double fs,
                    double f0, double qq, double gain_db)
{
    /* clamp to a stable, representable range */
    if (fs <= 0.0) fs = 48000.0;
    f0 = clampf(f0, 5.0, 0.49 * fs);
    qq = clampf(qq, 0.05, 40.0);
    gain_db = clampf(gain_db, -36.0, 36.0);

    const double w0 = 2.0 * M_PI * f0 / fs;
    const double cw = cos(w0);
    const double sw = sin(w0);
    const double alpha = sw / (2.0 * qq);
    const double A = pow(10.0, gain_db / 40.0);

    double b0, b1, b2, a0, a1, a2;
    switch (type) {
    case DTSX_BQ_LPF:
        b0 = (1.0 - cw) * 0.5; b1 = 1.0 - cw; b2 = b0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case DTSX_BQ_HPF:
        b0 = (1.0 + cw) * 0.5; b1 = -(1.0 + cw); b2 = b0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case DTSX_BQ_PEAK:
        b0 = 1.0 + alpha * A; b1 = -2.0 * cw; b2 = 1.0 - alpha * A;
        a0 = 1.0 + alpha / A; a1 = -2.0 * cw; a2 = 1.0 - alpha / A;
        break;
    case DTSX_BQ_LOWSHELF: {
        const double s = 2.0 * sqrt(A) * alpha;
        b0 = A * ((A + 1.0) - (A - 1.0) * cw + s);
        b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cw);
        b2 = A * ((A + 1.0) - (A - 1.0) * cw - s);
        a0 = (A + 1.0) + (A - 1.0) * cw + s;
        a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cw);
        a2 = (A + 1.0) + (A - 1.0) * cw - s;
        break;
    }
    case DTSX_BQ_HIGHSHELF: {
        const double s = 2.0 * sqrt(A) * alpha;
        b0 = A * ((A + 1.0) + (A - 1.0) * cw + s);
        b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cw);
        b2 = A * ((A + 1.0) + (A - 1.0) * cw - s);
        a0 = (A + 1.0) - (A - 1.0) * cw + s;
        a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cw);
        a2 = (A + 1.0) - (A - 1.0) * cw - s;
        break;
    }
    case DTSX_BQ_NOTCH:
        b0 = 1.0; b1 = -2.0 * cw; b2 = 1.0;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    case DTSX_BQ_ALLPASS:
        b0 = 1.0 - alpha; b1 = -2.0 * cw; b2 = 1.0 + alpha;
        a0 = 1.0 + alpha; a1 = -2.0 * cw; a2 = 1.0 - alpha;
        break;
    default:
        b0 = 1.0; b1 = 0.0; b2 = 0.0; a0 = 1.0; a1 = 0.0; a2 = 0.0;
        break;
    }

    if (!isfinite(a0) || fabs(a0) < 1e-12) {
        q->b0 = 1.0; q->b1 = q->b2 = q->a1 = q->a2 = 0.0;
        dtsx_bq_zero(q);
        return;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0;
    q->a1 = a1 / a0; q->a2 = a2 / a0;
    dtsx_bq_zero(q);
}

void dtsx_bq_zero(DtsxBiquad *q)
{
    q->z1 = 0.0f;
    q->z2 = 0.0f;
}

float dtsx_bq_tick(DtsxBiquad *q, float x)
{
    const float y = (float)(q->b0 * (double)x) + q->z1;
    q->z1 = (float)(q->b1 * (double)x - q->a1 * (double)y) + q->z2;
    q->z2 = (float)(q->b2 * (double)x - q->a2 * (double)y);
    /* flush denormal tails; cheap and keeps long silent passages fast */
    if (fabsf(q->z1) < 1e-20f) q->z1 = 0.0f;
    if (fabsf(q->z2) < 1e-20f) q->z2 = 0.0f;
    return y;
}
