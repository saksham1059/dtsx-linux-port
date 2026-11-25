/* Lookahead peak limiter with a sliding-maximum detector.
 *
 * A monotonic deque over the lookahead window provides the true peak that will
 * arrive within the next N samples, so gain reduction starts before the
 * transient reaches the output. A final soft clip guarantees the ceiling even
 * for inter-sample overshoots.
 */
#include "../dtsx_internal.h"
#include <stdlib.h>
#include <string.h>

int limiter_init(Limiter *li, const DtsxConfig *c, double fs)
{
    int n = (int)(fs * (c->lookahead_ms > 0.0 ? c->lookahead_ms : 0.0) / 1000.0);
    if (n < 1) n = 1;
    if (n > 4096) n = 4096;

    memset(li, 0, sizeof(*li));
    li->n = n;
    li->ceiling = (float)DTSX_DB2LIN(c->ceiling_db > -60.0 ? c->ceiling_db : -1.0);
    if (li->ceiling > 1.0f) li->ceiling = 1.0f;
    if (li->ceiling < 1e-3f) li->ceiling = 1e-3f;
    li->atk = (float)(1.0 - exp(-1.0 / (fs * c->limiter_attack_ms / 1000.0 + 1e-9)));
    li->rel = (float)(1.0 - exp(-1.0 / (fs * c->limiter_release_ms / 1000.0 + 1e-9)));
    if (li->atk > 1.0f) li->atk = 1.0f;
    li->dl = (float *)calloc((size_t)n, sizeof(float));
    li->dr = (float *)calloc((size_t)n, sizeof(float));
    li->win = (float *)calloc((size_t)n + 2, sizeof(float));
    li->wid = (int *)calloc((size_t)n + 2, sizeof(int));
    if (!li->dl || !li->dr || !li->win || !li->wid) {
        limiter_free(li);
        return -1;
    }
    li->gain = 1.0f;
    li->max_gain_reduction_db = 0.0f;
    return 0;
}

void limiter_free(Limiter *li)
{
    free(li->dl); free(li->dr); free(li->win); free(li->wid);
    li->dl = li->dr = NULL;
    li->win = NULL; li->wid = NULL;
    li->n = 0;
}

void limiter_reset(Limiter *li)
{
    if (li->dl && li->dr && li->n > 0) {
        memset(li->dl, 0, sizeof(float) * (size_t)li->n);
        memset(li->dr, 0, sizeof(float) * (size_t)li->n);
    }
    li->head = li->tail = 0;
    li->idx = 0;
    li->gain = 1.0f;
    li->max_gain_reduction_db = 0.0f;
}

static inline void deque_push(Limiter *li, float v, int pos)
{
    /* drop smaller maxima from the back */
    while (li->tail != li->head) {
        const int b = (li->tail - 1 + li->n + 2) % (li->n + 2);
        if (li->win[b] <= v) {
            li->tail = b;
        } else {
            break;
        }
    }
    li->win[li->tail] = v;
    li->wid[li->tail] = pos;
    li->tail = (li->tail + 1) % (li->n + 2);
}

static inline void deque_expire(Limiter *li, int oldest)
{
    while (li->tail != li->head) {
        if (li->wid[li->head] < oldest) {
            li->head = (li->head + 1) % (li->n + 2);
        } else {
            break;
        }
    }
}

void limiter_process(Limiter *li, float *l, float *r, size_t n)
{
    if (!li->dl || !li->dr) {
        for (size_t i = 0; i < n; i++) {
            l[i] = dtsx_soft_limit(l[i], li->ceiling);
            r[i] = dtsx_soft_limit(r[i], li->ceiling);
        }
        return;
    }
    for (size_t i = 0; i < n; i++) {
        const float inl = l[i], inr = r[i];
        const int pos = li->idx;
        const float peak = fmaxf(fabsf(inl), fabsf(inr));
        deque_push(li, peak, pos);
        deque_expire(li, pos - li->n + 1);

        float window_max = li->win[li->head];
        if (window_max < 1e-9f) window_max = 1e-9f;

        const float target = (window_max > li->ceiling)
                                 ? li->ceiling / window_max
                                 : 1.0f;
        const float coef = (target < li->gain) ? li->atk : li->rel;
        li->gain += coef * (target - li->gain);
        if (li->gain > 1.0f) li->gain = 1.0f;

        const float outl = li->dl[pos];
        const float outr = li->dr[pos];
        li->dl[pos] = inl;
        li->dr[pos] = inr;
        li->idx = (pos + 1) % li->n;

        const float gr = -20.0f * log10f(li->gain > 1e-6f ? li->gain : 1e-6f);
        if (gr > li->max_gain_reduction_db) li->max_gain_reduction_db = gr;

        l[i] = dtsx_soft_limit(outl * li->gain, li->ceiling);
        r[i] = dtsx_soft_limit(outr * li->gain, li->ceiling);
    }
}
