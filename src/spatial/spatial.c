/* Stereo width and headphone crossfeed.
 *
 * Width is a mid/side gain. Crossfeed follows the classic Bauer/BS2B idea:
 * a low-passed, short-delayed copy of the opposite channel is added while the
 * same-side low-passed signal is subtracted, which keeps mono content intact
 * and only affects lateral imaging.
 */
#include "../dtsx_internal.h"
#include <stdlib.h>
#include <string.h>

int spatial_init(Spatial *s, const DtsxConfig *c, double fs)
{
    s->fs = fs;
    sm_init(&s->width, (float)c->width, fs, 60.0);
    sm_init(&s->xf, (float)c->crossfeed, fs, 60.0);
    dtsx_bq_design(&s->lpL, DTSX_BQ_LPF, fs, 700.0, 0.7071067811865476, 0.0);
    dtsx_bq_design(&s->lpR, DTSX_BQ_LPF, fs, 700.0, 0.7071067811865476, 0.0);
    s->dlen = 0;
    s->pos = 0;
    s->dl = s->dr = NULL;
    /* The fixed delay storage is allocated at chain creation even if
     * crossfeed starts at zero. This makes live crossfeed changes allocation
     * free and therefore safe at an audio block boundary. */
    {
        int n = (int)(fs * 0.00035); /* ~0.35 ms inter-aural delay */
        if (n < 1) n = 1;
        s->dl = (float *)calloc((size_t)n, sizeof(float));
        s->dr = (float *)calloc((size_t)n, sizeof(float));
        if (!s->dl || !s->dr) {
            free(s->dl); free(s->dr);
            s->dl = s->dr = NULL;
            return -1;
        }
        s->dlen = n;
    }
    return 0;
}

void spatial_free(Spatial *s)
{
    free(s->dl); free(s->dr);
    s->dl = s->dr = NULL;
    s->dlen = 0;
}

void spatial_reset(Spatial *s)
{
    dtsx_bq_zero(&s->lpL);
    dtsx_bq_zero(&s->lpR);
    if (s->dl && s->dr && s->dlen > 0) {
        memset(s->dl, 0, sizeof(float) * (size_t)s->dlen);
        memset(s->dr, 0, sizeof(float) * (size_t)s->dlen);
    }
    s->pos = 0;
}

void spatial_set_width(Spatial *s, double w)
{
    if (w < 0.0) w = 0.0;
    if (w > 2.0) w = 2.0;
    sm_set(&s->width, (float)w);
}

void spatial_process(Spatial *s, float *l, float *r, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        const float w = sm_tick(&s->width);
        const float xf = sm_tick(&s->xf);
        float sl = l[i], sr = r[i];
        const float m = 0.5f * (sl + sr);
        const float sdl = 0.5f * (sl - sr) * w;
        sl = m + sdl;
        sr = m - sdl;
        if (xf > 0.0001f) {
            const float cl = dtsx_bq_tick(&s->lpL, sl);
            const float cr = dtsx_bq_tick(&s->lpR, sr);
            const float dl = s->dl[s->pos];
            const float dr = s->dr[s->pos];
            s->dl[s->pos] = sl;
            s->dr[s->pos] = sr;
            s->pos = (s->pos + 1) % s->dlen;
            sl = sl + xf * (dr - cr);
            sr = sr + xf * (dl - cl);
        }
        l[i] = sl;
        r[i] = sr;
    }
}
