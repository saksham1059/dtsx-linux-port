/* LADSPA ABI smoke test: load the built plugin, instantiate both labels,
 * run audio, and require finite bounded output. */
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ladspa.h"
#include "dtsx_live.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

typedef const LADSPA_Descriptor *(*desc_fn)(unsigned long);
typedef DtsxLiveHandle *(*live_open_fn)(const char *, int);
typedef void (*live_close_fn)(DtsxLiveHandle *);
typedef int (*live_read_fn)(DtsxLiveHandle *, DtsxLiveParams *, uint32_t *);
typedef int (*live_publish_fn)(DtsxLiveHandle *, const DtsxLiveParams *);

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "build/dtsx_ladspa.so";
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    desc_fn fn = (desc_fn)dlsym(h, "ladspa_descriptor");
    if (!fn) { fprintf(stderr, "no ladspa_descriptor\n"); return 1; }

    live_open_fn live_open = (live_open_fn)dlsym(h, "dtsx_live_open");
    live_close_fn live_close = (live_close_fn)dlsym(h, "dtsx_live_close");
    live_read_fn live_read = (live_read_fn)dlsym(h, "dtsx_live_read");
    live_publish_fn live_publish = (live_publish_fn)dlsym(h, "dtsx_live_publish");
    int failures = (!live_open || !live_close || !live_read || !live_publish);
    if (failures) printf("FAIL: live parameter API missing from plugin\n");
    for (unsigned long idx = 0; idx < 2; idx++) {
        const LADSPA_Descriptor *d = fn(idx);
        if (!d) { printf("FAIL: descriptor %lu missing\n", idx); failures++; continue; }
        printf("label[%lu] = %s (%lu ports)\n", idx, d->Label, d->PortCount);
        LADSPA_Handle inst = d->instantiate(d, 48000);
        if (!inst) { printf("FAIL: instantiate %s\n", d->Label); failures++; continue; }
        float inl[256], inr[256], outl[256], outr[256];
        float bypass = 0.0f;
        for (unsigned long p = 0; p < d->PortCount; p++) {
            if (!strcmp(d->PortNames[p], "In L")) d->connect_port(inst, p, inl);
            else if (!strcmp(d->PortNames[p], "In R")) d->connect_port(inst, p, inr);
            else if (!strcmp(d->PortNames[p], "Out L")) d->connect_port(inst, p, outl);
            else if (!strcmp(d->PortNames[p], "Out R")) d->connect_port(inst, p, outr);
            else if (!strcmp(d->PortNames[p], "Bypass")) d->connect_port(inst, p, &bypass);
        }
        d->activate(inst);
        int finite = 1;
        float peak = 0.0f;
        unsigned s = 99u;
        for (int b = 0; b < 400; b++) {
            for (int i = 0; i < 256; i++) {
                s = s * 1103515245u + 12345u;
                const float x = ((float)((s >> 8) & 0xffff) / 32768.0f - 1.0f) * 0.7f;
                inl[i] = x;
                inr[i] = -x;
            }
            d->run(inst, 256);
            for (int i = 0; i < 256; i++) {
                if (!isfinite(outl[i]) || !isfinite(outr[i])) finite = 0;
                float p = fmaxf(fabsf(outl[i]), fabsf(outr[i]));
                if (p > peak) peak = p;
            }
        }
        /* Prove the LADSPA instance consumes the shared snapshot: publish a
         * bypass change, then require the next block to be exact passthrough.
         * This validates an actual processing change, not just shm access. */
        if (live_open) {
            DtsxLiveHandle *live = live_open(idx == 0 ? "speakers" : "headphones", 0);
            DtsxLiveParams p;
            uint32_t seq = 0;
            int live_ok = live && live_read(live, &p, &seq) == 1;
            if (live_ok) {
                p.bypass = 1;
                live_ok = live_publish(live, &p) == 0;
                for (int i = 0; i < 256; i++) inl[i] = 0.15f * sinf((float)i * 0.13f), inr[i] = -inl[i];
                d->run(inst, 256);
                for (int i = 0; i < 256; i++)
                    if (fabsf(outl[i] - inl[i]) > 1e-6f || fabsf(outr[i] - inr[i]) > 1e-6f) live_ok = 0;

                /* Restore processing, then require a +12 dB 1 kHz graphic
                 * EQ change to produce a measurable output change. */
                p.bypass = 0;
                p.user_eq_enable = 0;
                for (int i = 0; i < DTSX_USER_EQ_BANDS; i++) p.user_eq_g[i] = 0.0f;
                live_ok = live_ok && live_publish(live, &p) == 0;
                double base = 0.0, boosted = 0.0;
                for (int b = 0; b < 40; b++) {
                    for (int i = 0; i < 256; i++) inl[i] = inr[i] = 0.02f * sinf(2.0f * (float)M_PI * 1000.0f * (float)i / 48000.0f);
                    d->run(inst, 256);
                    if (b > 20) for (int i = 0; i < 256; i++) base += (double)outl[i] * outl[i];
                }
                p.user_eq_enable = 1;
                p.user_eq_g[5] = 12.0f;
                live_ok = live_ok && live_publish(live, &p) == 0;
                for (int b = 0; b < 80; b++) {
                    for (int i = 0; i < 256; i++) inl[i] = inr[i] = 0.02f * sinf(2.0f * (float)M_PI * 1000.0f * (float)i / 48000.0f);
                    d->run(inst, 256);
                    if (b > 50) for (int i = 0; i < 256; i++) boosted += (double)outl[i] * outl[i];
                }
                live_ok = live_ok && boosted > base * 2.0;
                printf("  %s: live-EQ energy ratio=%.2f\n", d->Label,
                       base > 0.0 ? boosted / base : 0.0);
            }
            if (live) live_close(live);
            printf("  %s: live-bypass=%d\n", d->Label, live_ok);
            if (!live_ok) { printf("FAIL: %s did not apply live snapshot\n", d->Label); failures++; }
        }
        printf("  %s: finite=%d peak=%.4f\n", d->Label, finite, peak);
        if (!finite || peak > 1.01f) { printf("FAIL: %s unstable\n", d->Label); failures++; }
        d->deactivate(inst);
        d->cleanup(inst);
    }
    dlclose(h);
    printf(failures ? "LADSPA TEST FAILED\n" : "LADSPA TEST PASSED\n");
    return failures ? 1 : 0;
}
