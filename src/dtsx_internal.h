/* Internal module boundaries for dtsx. Not installed. */
#ifndef DTSX_INTERNAL_H
#define DTSX_INTERNAL_H

#include "dtsx.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DTSX_DB2LIN(x) pow(10.0, (x) / 20.0)
#define DTSX_LIN2DB(x) (20.0 * log10((x) > 1e-30 ? (x) : 1e-30))

/* --------------------------------------------------------------- smoothing */
typedef struct { float cur, target, coef; } Sm;

static inline void sm_init(Sm *s, float v, double fs, double ms)
{
    s->cur = s->target = v;
    s->coef = (float)(1.0 - exp(-1.0 / (fs * ms / 1000.0 + 1e-12)));
}
static inline void sm_set(Sm *s, float v) { s->target = v; }
static inline float sm_tick(Sm *s)
{
    s->cur += s->coef * (s->target - s->cur);
    return s->cur;
}

/* --------------------------------------------------------------- crossover */
/* Linkwitz-Riley 4th order: two cascaded Butterworth sections. */
typedef struct { DtsxBiquad l1, l2, h1, h2; } Lr4;
void lr4_init(Lr4 *x, double fs, double f);
void lr4_reset(Lr4 *x);
void lr4_split(Lr4 *x, float in, float *lo, float *hi);

/* --------------------------------------------------------- bass enhancement */
typedef struct {
    int enabled;
    double fs;
    Sm amount, drive, mix;
    DtsxBiquad lp, hp, shaper, ext, size_hp;
    float dc, dc_coef;
} TruBass;

void  truhdx_init(TruBass *t, const DtsxConfig *c, double fs);
void  truhdx_reset(TruBass *t);
float truhdx_tick(TruBass *t, float x);

/* ------------------------------------------------------------------- MBHL */
typedef struct {
    float env;       /* linear envelope */
    float gr_db;     /* smoothed gain reduction */
    float atk, rel;  /* one-pole coefficients */
    float thresh_db, ratio, makeup; /* makeup is linear */
} Comp;

typedef struct {
    Lr4 split_lo, split_mid;
    Comp lo, mid, hi;
} MbhlChannel;

typedef struct {
    int enabled;
    double fs;
    MbhlChannel ch[2];
    float boost_lin;
    float mix;
} Mbhl;

void mbhl_init(Mbhl *m, const DtsxConfig *c, double fs);
void mbhl_reset(Mbhl *m);
void mbhl_process(Mbhl *m, float *l, float *r, size_t n);
void mbhl_set_amount(Mbhl *m, double amount01);

/* --------------------------------------------------------------- loudness */
typedef struct {
    int enabled;
    double fs;
    Sm amount;               /* 0..1 effective compensation */
    DtsxBiquad lo, hi;
    DtsxBiquad lo2, hi2;     /* stereo uses separate sets */
    unsigned ctr;
    float last_gain;
} Loudness;

void loudness_init(Loudness *l, const DtsxConfig *c, double fs);
void loudness_reset(Loudness *l);
void loudness_process(Loudness *l, float *lch, float *rch, size_t n);
void loudness_set(Loudness *l, double amount01);

/* ---------------------------------------------------------------- spatial */
typedef struct {
    Sm width, xf;
    DtsxBiquad lpL, lpR;
    float *dl, *dr;
    int dlen, pos;
    double fs;
} Spatial;

int  spatial_init(Spatial *s, const DtsxConfig *c, double fs);
void spatial_free(Spatial *s);
void spatial_reset(Spatial *s);
void spatial_process(Spatial *s, float *l, float *r, size_t n);
void spatial_set_width(Spatial *s, double w);

/* ---------------------------------------------------------------- limiter */
typedef struct {
    int n;                 /* lookahead samples */
    float ceiling;
    float atk, rel;
    float *dl, *dr;        /* delay lines */
    float *win;            /* sliding-window maxima (monotonic deque) */
    int *wid;              /* sample indices for the deque */
    int head, tail, idx;   /* ring bookkeeping */
    float gain;            /* smoothed gain */
    float max_gain_reduction_db;
} Limiter;

int  limiter_init(Limiter *li, const DtsxConfig *c, double fs);
void limiter_free(Limiter *li);
void limiter_reset(Limiter *li);
void limiter_process(Limiter *li, float *l, float *r, size_t n);

/* ---------------------------------------------------------------- helpers */
float dtsx_soft_limit(float x, float ceiling);
double dtsx_biquad_response_db(const DtsxBiquad *q, double fs, double hz);

#endif /* DTSX_INTERNAL_H */
