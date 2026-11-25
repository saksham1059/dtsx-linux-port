/* dtsx_live.h — cross-process live parameter publication for the dtsx chain.
 *
 * The desktop application (Rust) and the LADSPA plugin (C, inside the PipeWire
 * process) share a small POSIX shared-memory page. The writer publishes a
 * complete fixed-size snapshot under a sequence counter (seqlock); the reader
 * never blocks, never allocates and never performs I/O in the audio callback.
 *
 * The binary layout is a public ABI between:
 *   - desktop/src-tauri/src/params.rs  (Rust writer)
 *   - src/live/live.c                  (C reader/writer)
 *
 * Keep the two definitions in sync. The layout is versioned and the reader
 * validates magic, version and struct size before applying anything.
 */
#ifndef DTSX_LIVE_H
#define DTSX_LIVE_H

#include <stdint.h>
#include <stddef.h>
#include "dtsx.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DTSX_LIVE_MAGIC 0x4454534Cu /* "DTSL" */
#define DTSX_LIVE_ABI_VERSION 1u
#define DTSX_LIVE_MAP_BYTES 4096u

#define DTSX_LIVE_CAP_CORRECTION_EQ (1u << 0)
#define DTSX_LIVE_CAP_USER_EQ       (1u << 1)
#define DTSX_LIVE_CAP_BASS          (1u << 2)
#define DTSX_LIVE_CAP_DYNAMICS      (1u << 3)
#define DTSX_LIVE_CAP_LOUDNESS      (1u << 4)
#define DTSX_LIVE_CAP_SPATIAL       (1u << 5)
#define DTSX_LIVE_CAP_GAIN          (1u << 6)
#define DTSX_LIVE_CAP_LIMITER       (1u << 7)

#define DTSX_LIVE_CAPABILITIES                                              \
    (DTSX_LIVE_CAP_CORRECTION_EQ | DTSX_LIVE_CAP_USER_EQ |                  \
     DTSX_LIVE_CAP_BASS | DTSX_LIVE_CAP_DYNAMICS | DTSX_LIVE_CAP_LOUDNESS | \
     DTSX_LIVE_CAP_SPATIAL | DTSX_LIVE_CAP_GAIN | DTSX_LIVE_CAP_LIMITER)

typedef struct {
    /* The sequence counter MUST stay first and naturally aligned: both sides
     * address it with acquire/release atomics. */
    volatile uint32_t seq;
    uint32_t magic;
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t flags;          /* bit 0: publisher has published at least once */
    uint32_t reserved0;

    int32_t bypass;
    int32_t hpf_enable;
    int32_t hpf_order;
    int32_t eq_enable;
    int32_t eq_n;
    int32_t user_eq_enable;
    int32_t bass_enable;
    int32_t mbhl_enable;
    int32_t loud_enable;
    int32_t reserved1;

    int32_t eq_type[DTSX_MAX_BANDS];
    float eq_f[DTSX_MAX_BANDS];
    float eq_q[DTSX_MAX_BANDS];
    float eq_g[DTSX_MAX_BANDS];

    int32_t user_eq_type[DTSX_USER_EQ_BANDS];
    float user_eq_f[DTSX_USER_EQ_BANDS];
    float user_eq_q[DTSX_USER_EQ_BANDS];
    float user_eq_g[DTSX_USER_EQ_BANDS];

    float input_gain_db;
    float output_gain_db;
    float hpf_hz;

    float bass_amount;
    float bass_drive;
    float bass_lo_hz;
    float bass_harm_hp_hz;
    float bass_extend_db;
    float bass_size_hz;
    float bass_harm_mix;

    float mbhl_low_x;
    float mbhl_mid_x;
    float mbhl_attack_ms;
    float mbhl_low_decay_ms;
    float mbhl_mid_decay_ms;
    float mbhl_high_decay_ms;
    float mbhl_low_ratio;
    float mbhl_mid_ratio;
    float mbhl_high_ratio;
    float mbhl_low_thresh_db;
    float mbhl_mid_thresh_db;
    float mbhl_high_thresh_db;
    float mbhl_low_makeup_db;
    float mbhl_mid_makeup_db;
    float mbhl_high_makeup_db;
    float mbhl_boost_db;

    float loud_amount;
    float loud_volume;
    float width;
    float crossfeed;
    float ceiling_db;

    float limiter_attack_ms;   /* reported; reload-only */
    float limiter_release_ms;  /* reported; reload-only */
    float lookahead_ms;        /* reported; reload-only */
    uint32_t capabilities;
    uint32_t reserved2[24];
} DtsxLiveParams;

typedef struct DtsxLiveHandle DtsxLiveHandle;

/* Open (and optionally create) the shared page for "speakers" or "headphones".
 * DTSX_LIVE_NAMESPACE may isolate tests from the live desktop instance. */
DtsxLiveHandle *dtsx_live_open(const char *label, int create_if_missing);
void dtsx_live_close(DtsxLiveHandle *h);

/* Read a consistent snapshot. Returns 1 when a new snapshot was copied,
 * 0 when unchanged or no valid snapshot was available. */
int dtsx_live_read(DtsxLiveHandle *h, DtsxLiveParams *out, uint32_t *last_seq);

/* Publish a complete snapshot (writer side; used by tests and by the plugin's
 * initial publication). Block-boundary / control-thread only. */
int dtsx_live_publish(DtsxLiveHandle *h, const DtsxLiveParams *p);

/* Fill a snapshot from a chain configuration and vice versa. The latter
 * defensively clamps every live parameter and preserves reload-only fields
 * (sample rate, lookahead, limiter attack/release) from the target config. */
void dtsx_live_params_from_config(const DtsxConfig *c, DtsxLiveParams *p);
int  dtsx_live_params_to_config(const DtsxLiveParams *p, DtsxConfig *c);

size_t dtsx_live_struct_size(void);
size_t dtsx_live_map_bytes(void);

#ifdef __cplusplus
}
#endif
#endif
