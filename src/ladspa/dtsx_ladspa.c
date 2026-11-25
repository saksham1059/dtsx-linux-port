/* LADSPA stereo plugin wrapping the dtsx processing chain.
 *
 * Two labels are exported so speaker and headphone profiles can coexist:
 *   dtsx_speakers   -> ~/.config/dtsx/speakers.json
 *   dtsx_headphones -> ~/.config/dtsx/headphones.json
 * $DTSX_CONFIG overrides the profile path (useful for offline testing).
 *
 * PipeWire filter-chain:
 *   { type = ladspa name = dtsx plugin = "/abs/path/dtsx_ladspa"
 *     label = "dtsx_speakers" }
 */
#include "dtsx.h"
#include "dtsx_live.h"
#include "ladspa.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#if defined(__x86_64__) && defined(__SSE__)
#include <xmmintrin.h>
static void enable_ftz(void) { _mm_setcsr(_mm_getcsr() | 0x8040u); }
#else
static void enable_ftz(void) {}
#endif

enum {
    PORT_IN_L = 0,
    PORT_IN_R,
    PORT_OUT_L,
    PORT_OUT_R,
    PORT_BYPASS,
    PORT_COUNT
};

typedef struct {
    DtsxConfig cfg;
    DtsxChain *chain;
    LADSPA_Data *port[PORT_COUNT];
    const char *profile;
    DtsxLiveHandle *live;
    uint32_t live_seq;
} DtsxPlugin;

static void load_profile(DtsxConfig *cfg, unsigned long rate, const char *profile)
{
    dtsx_config_defaults(cfg, (double)rate);
    const char *path = getenv("DTSX_CONFIG");
    char buf[1024];
    if (!path || !*path) {
        const char *home = getenv("HOME");
        if (home && *home && profile) {
            snprintf(buf, sizeof buf, "%s/.config/dtsx/%s.json", home, profile);
            path = buf;
        }
    }
    if (path && *path) {
        if (dtsx_config_load(cfg, path) != 0)
            fprintf(stderr, "dtsx_ladspa: cannot read %s, using defaults\n", path);
    }
    cfg->fs = (double)rate;
}

static LADSPA_Handle dtsx_instantiate(const LADSPA_Descriptor *desc, unsigned long rate)
{
    enable_ftz();
    DtsxPlugin *p = (DtsxPlugin *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->profile = (const char *)(desc ? desc->ImplementationData : NULL);
    load_profile(&p->cfg, rate, p->profile);
    p->chain = dtsx_chain_create(&p->cfg, (double)rate);
    if (!p->chain) { free(p); return NULL; }

    /* Open once during instantiation. The audio callback only performs a
     * bounded lock-free snapshot read; it never opens files or allocates. */
    p->live = dtsx_live_open(p->profile, 1);
    if (p->live) {
        DtsxLiveParams snapshot;
        if (dtsx_live_read(p->live, &snapshot, &p->live_seq)) {
            DtsxConfig next = p->cfg;
            if (dtsx_live_params_to_config(&snapshot, &next) == 0)
                (void)dtsx_chain_live_apply(p->chain, &next);
        } else {
            dtsx_live_params_from_config(&p->cfg, &snapshot);
            (void)dtsx_live_publish(p->live, &snapshot);
        }
    }
    return (LADSPA_Handle)p;
}

static void dtsx_connect_port(LADSPA_Handle h, unsigned long port, LADSPA_Data *data)
{
    DtsxPlugin *p = (DtsxPlugin *)h;
    if (p && port < PORT_COUNT) p->port[port] = data;
}

static void dtsx_activate(LADSPA_Handle h)
{
    DtsxPlugin *p = (DtsxPlugin *)h;
    if (!p) return;
    dtsx_chain_reset(p->chain);
}

static void dtsx_run(LADSPA_Handle h, unsigned long n)
{
    DtsxPlugin *p = (DtsxPlugin *)h;
    if (!p || !p->port[PORT_OUT_L] || !p->port[PORT_OUT_R] || n == 0) return;

    float *l = p->port[PORT_OUT_L];
    float *r = p->port[PORT_OUT_R];
    const float *il = p->port[PORT_IN_L];
    const float *ir = p->port[PORT_IN_R];

    if (p->port[PORT_BYPASS]) dtsx_chain_set_bypass(p->chain, *p->port[PORT_BYPASS] > 0.5f);

    if (p->live) {
        DtsxLiveParams snapshot;
        if (dtsx_live_read(p->live, &snapshot, &p->live_seq)) {
            DtsxConfig next = p->cfg;
            if (dtsx_live_params_to_config(&snapshot, &next) == 0) {
                (void)dtsx_chain_live_apply(p->chain, &next);
                p->cfg = next;
            }
        }
    }

    if (il && il != l) memcpy(l, il, sizeof(float) * n);
    if (ir && ir != r) memcpy(r, ir, sizeof(float) * n);
    if (!il) memset(l, 0, sizeof(float) * n);
    if (!ir) memset(r, 0, sizeof(float) * n);

    dtsx_chain_process(p->chain, l, r, (size_t)n);
}

static void dtsx_deactivate(LADSPA_Handle h)
{
    DtsxPlugin *p = (DtsxPlugin *)h;
    if (p) dtsx_chain_reset(p->chain);
}

static void dtsx_cleanup(LADSPA_Handle h)
{
    DtsxPlugin *p = (DtsxPlugin *)h;
    if (!p) return;
    dtsx_live_close(p->live);
    dtsx_chain_destroy(p->chain);
    free(p);
}

/* ------------------------------------------------------------------ tables */
static const char *const g_port_names[PORT_COUNT] = {
    "In L", "In R", "Out L", "Out R", "Bypass"
};

static const LADSPA_PortDescriptor g_port_desc[PORT_COUNT] = {
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_OUTPUT | LADSPA_PORT_AUDIO,
    LADSPA_PORT_INPUT | LADSPA_PORT_CONTROL
};

static const LADSPA_PortRangeHint g_port_hint[PORT_COUNT] = {
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {0, 0, 0},
    {LADSPA_HINT_TOGGLED | LADSPA_HINT_DEFAULT_0 | LADSPA_HINT_BOUNDED_BELOW | LADSPA_HINT_BOUNDED_ABOVE, 0.0f, 1.0f}
};

static const LADSPA_Descriptor g_desc_speakers = {
    0x44545301UL, "dtsx_speakers", LADSPA_PROPERTY_HARD_RT_CAPABLE,
    "DTS:X Ultra-inspired speaker processor",
    "Independent clean-room reconstruction", "GPL-2.0-or-later",
    PORT_COUNT, g_port_desc, g_port_names, g_port_hint,
    (void *)"speakers",
    dtsx_instantiate, dtsx_connect_port, dtsx_activate, dtsx_run,
    NULL, NULL, dtsx_deactivate, dtsx_cleanup
};

static const LADSPA_Descriptor g_desc_headphones = {
    0x44545302UL, "dtsx_headphones", LADSPA_PROPERTY_HARD_RT_CAPABLE,
    "DTS:X Ultra-inspired headphone processor",
    "Independent clean-room reconstruction", "GPL-2.0-or-later",
    PORT_COUNT, g_port_desc, g_port_names, g_port_hint,
    (void *)"headphones",
    dtsx_instantiate, dtsx_connect_port, dtsx_activate, dtsx_run,
    NULL, NULL, dtsx_deactivate, dtsx_cleanup
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long index)
{
    if (index == 0) return &g_desc_speakers;
    if (index == 1) return &g_desc_headphones;
    return NULL;
}
