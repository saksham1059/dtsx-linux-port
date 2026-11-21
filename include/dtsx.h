/* dtsx.h — independent, DTS:X Ultra-inspired audio processor.
 *
 * Clean-room implementation: no proprietary code is linked or executed.
 * The speaker correction curves and control ranges were derived from
 * static analysis and the OEM configuration profile; see
 * docs/REVERSE_ENGINEERING_FINDINGS.md and docs/DSP_DESIGN.md.
 *
 * Real-time rules for dtsx_chain_process():
 *   - no allocation, no locks, no I/O, no logging
 *   - bounded work per sample, all state preallocated
 */
#ifndef DTSX_H
#define DTSX_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DTSX_MAX_BANDS 24
#define DTSX_MAX_CHANNELS 2

/* Safety cap for the TruBass-inspired harmonic stage. The project has always
 * documented 0.55 as the maximum safe amount for laptop speakers; enforce it in
 * the engine as well as in the UI. */
#define DTSX_BASS_AMOUNT_MAX 0.55

/* The desktop application exposes a ten-band graphic user EQ. The frequencies
 * are fixed ISO-style centres; the per-band gains live in the profile. This is
 * a separate stage from the 19-band OEM-derived correction curve so that user
 * tone changes never destroy the evidence-based speaker correction. */
#define DTSX_USER_EQ_BANDS 10
extern const double dtsx_user_eq_freqs[DTSX_USER_EQ_BANDS];

/* ------------------------------------------------------------------ biquad */
typedef struct {
    double b0, b1, b2, a1, a2; /* a0 normalised out */
    float z1, z2;              /* transposed direct form II state */
} DtsxBiquad;

typedef enum {
    DTSX_BQ_PEAK = 0,
    DTSX_BQ_LOWSHELF,
    DTSX_BQ_HIGHSHELF,
    DTSX_BQ_HPF,
    DTSX_BQ_LPF,
    DTSX_BQ_NOTCH,
    DTSX_BQ_ALLPASS
} DtsxBiquadType;

void dtsx_bq_design(DtsxBiquad *q, DtsxBiquadType type, double fs,
                    double f0, double qq, double gain_db);
void dtsx_bq_zero(DtsxBiquad *q);
float dtsx_bq_tick(DtsxBiquad *q, float x);

typedef struct {
    int type;      /* DtsxBiquadType */
    double f;      /* Hz */
    double q;      /* Q */
    double gain;   /* dB */
} DtsxEqBand;

/* ------------------------------------------------------------------ config */
typedef struct {
    double fs;

    double input_gain_db;
    double output_gain_db;

    int    hpf_enable;
    double hpf_hz;
    int    hpf_order;          /* 2 or 4 */

    int    eq_enable;
    int    eq_n;
    DtsxEqBand eq[DTSX_MAX_BANDS];

    /* Ten-band user graphic EQ, independent of the correction EQ above. */
    int    user_eq_enable;
    double user_eq_g[DTSX_USER_EQ_BANDS];

    /* TruBass HDX-inspired harmonic bass enhancement */
    int    bass_enable;
    double bass_amount;        /* 0..1, mapped from "Bass Level" */
    double bass_drive;         /* 0.5..4 */
    double bass_lo_hz;         /* low-band split for harmonic generation */
    double bass_harm_hp_hz;    /* removes rectifier DC / rumble */
    double bass_extend_db;     /* low-shelf makeup, "Extended Bass" */
    double bass_size_hz;       /* effective speaker-size tuning */
    double bass_harm_mix;      /* 0..1 dry/harmonic balance */

    /* MBHL-inspired 3-band dynamics */
    int    mbhl_enable;
    double mbhl_low_x, mbhl_mid_x;
    double mbhl_attack_ms;
    double mbhl_low_decay_ms, mbhl_mid_decay_ms, mbhl_high_decay_ms;
    double mbhl_low_ratio, mbhl_mid_ratio, mbhl_high_ratio;
    double mbhl_low_thresh_db, mbhl_mid_thresh_db, mbhl_high_thresh_db;
    double mbhl_low_makeup_db, mbhl_mid_makeup_db, mbhl_high_makeup_db;
    double mbhl_boost_db;

    /* Loudness compensation */
    int    loud_enable;
    double loud_amount;        /* 0..1 */
    double loud_volume;        /* 0 = loud, 1 = quiet */

    /* Spatial */
    double width;              /* 1.0 = unchanged */
    double crossfeed;          /* 0..1 headphone crossfeed */

    /* Output protection */
    double ceiling_db;
    double limiter_attack_ms;
    double limiter_release_ms;
    double lookahead_ms;

    int    bypass;
} DtsxConfig;

int  dtsx_config_defaults(DtsxConfig *c, double fs);
int  dtsx_config_load(DtsxConfig *c, const char *path);
int  dtsx_config_write(const DtsxConfig *c, const char *path);
void dtsx_config_print(const DtsxConfig *c, void *file);

/* ------------------------------------------------------------------- chain */
typedef struct DtsxChain DtsxChain;

DtsxChain *dtsx_chain_create(const DtsxConfig *cfg, double fs);
void       dtsx_chain_destroy(DtsxChain *ch);
void       dtsx_chain_process(DtsxChain *ch, float *left, float *right, size_t frames);
/* block-safe control: smoothed internally, no allocation */
void       dtsx_chain_set_bass(DtsxChain *ch, double amount01);
void       dtsx_chain_set_loudness(DtsxChain *ch, double amount01);
void       dtsx_chain_set_width(DtsxChain *ch, double width);
void       dtsx_chain_set_bypass(DtsxChain *ch, int bypass);
void       dtsx_chain_reset(DtsxChain *ch);

/* Apply a complete live parameter snapshot at a block boundary.
 *
 * Real-time rules: no allocation, no locks, no file descriptor access and no
 * logging. Continuous controls are smoothed; equaliser coefficients are
 * refreshed at a bounded control rate with filter state preserved. Parameters
 * that would require reallocating the lookahead delay line or changing the
 * sample rate are deliberately ignored (the UI reports them as reload-only).
 *
 * Returns a bitmask: DTSX_LIVE_APPLIED if anything changed, DTSX_LIVE_DEFERRED
 * if a reload-only field differed. */
#define DTSX_LIVE_APPLIED  0x1u
#define DTSX_LIVE_DEFERRED 0x2u
unsigned   dtsx_chain_live_apply(DtsxChain *ch, const DtsxConfig *cfg);

/* latency introduced by the lookahead limiter, in samples */
unsigned   dtsx_chain_latency(const DtsxChain *ch);

/* response of the static equaliser section (for validation/plotting) */
double     dtsx_chain_eq_response_db(const DtsxConfig *cfg, double fs, double hz);

#ifdef __cplusplus
}
#endif

#endif /* DTSX_H */
