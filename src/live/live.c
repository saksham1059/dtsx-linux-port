/* Live parameter shared-memory transport.
 *
 * This file is deliberately free of any audio processing. The plugin reads a
 * snapshot with dtsx_live_read() at a block boundary and hands the resulting
 * configuration to dtsx_chain_live_apply(). No function here is called from
 * inside the per-sample loop.
 */
#define _POSIX_C_SOURCE 200809L
#include "dtsx_live.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

struct DtsxLiveHandle {
    int fd;
    char name[256];
    DtsxLiveParams *map;
    size_t map_bytes;
};

static int valid_label_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
}

static int sanitize(const char *in, char *out, size_t cap)
{
    if (!in || !*in) return -1;
    size_t n = 0;
    for (; *in; in++) {
        if (!valid_label_char(*in)) return -1;
        if (n + 1 >= cap) return -1;
        out[n++] = *in;
    }
    out[n] = 0;
    return 0;
}

static int make_name(const char *label, char *out, size_t cap)
{
    char lab[64];
    char ns[64];
    if (sanitize(label, lab, sizeof lab) != 0) return -1;
    ns[0] = 0;
    const char *env = getenv("DTSX_LIVE_NAMESPACE");
    if (env && *env) {
        if (sanitize(env, ns, sizeof ns) != 0) return -1;
    }
    const unsigned long uid = (unsigned long)getuid();
    int n;
    if (ns[0])
        n = snprintf(out, cap, "/dtsx-live-v1-%lu-%s-%s", uid, ns, lab);
    else
        n = snprintf(out, cap, "/dtsx-live-v1-%lu-%s", uid, lab);
    return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

size_t dtsx_live_struct_size(void) { return sizeof(DtsxLiveParams); }
size_t dtsx_live_map_bytes(void) { return (size_t)DTSX_LIVE_MAP_BYTES; }

DtsxLiveHandle *dtsx_live_open(const char *label, int create_if_missing)
{
    if (sizeof(DtsxLiveParams) > DTSX_LIVE_MAP_BYTES) return NULL;
    char name[256];
    if (make_name(label, name, sizeof name) != 0) return NULL;

    int fd = -1;
    int created = 0;
    if (create_if_missing) {
        fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            created = 1;
        } else if (errno == EEXIST) {
            fd = shm_open(name, O_RDWR, 0600);
        }
    } else {
        fd = shm_open(name, O_RDWR, 0600);
    }
    if (fd < 0) return NULL;

    struct stat st;
    if (fstat(fd, &st) != 0) { close(fd); return NULL; }
    if ((unsigned long)st.st_uid != (unsigned long)getuid()) {
        close(fd);
        return NULL;
    }
    if (created || st.st_size < (off_t)DTSX_LIVE_MAP_BYTES) {
        if (ftruncate(fd, (off_t)DTSX_LIVE_MAP_BYTES) != 0) {
            close(fd);
            return NULL;
        }
        (void)fchmod(fd, 0600);
    }

    void *mem = mmap(NULL, DTSX_LIVE_MAP_BYTES, PROT_READ | PROT_WRITE,
                     MAP_SHARED, fd, 0);
    if (mem == MAP_FAILED) { close(fd); return NULL; }

    DtsxLiveHandle *h = (DtsxLiveHandle *)calloc(1, sizeof(*h));
    if (!h) { munmap(mem, DTSX_LIVE_MAP_BYTES); close(fd); return NULL; }
    h->fd = fd;
    h->map = (DtsxLiveParams *)mem;
    h->map_bytes = DTSX_LIVE_MAP_BYTES;
    memcpy(h->name, name, sizeof name);

    if (created || st.st_size < (off_t)sizeof(DtsxLiveParams)) {
        memset(mem, 0, DTSX_LIVE_MAP_BYTES);
        h->map->magic = DTSX_LIVE_MAGIC;
        h->map->abi_version = DTSX_LIVE_ABI_VERSION;
        h->map->struct_size = (uint32_t)sizeof(DtsxLiveParams);
        h->map->flags = 0;
        h->map->seq = 0;
    } else {
        if (h->map->magic != DTSX_LIVE_MAGIC ||
            h->map->abi_version != DTSX_LIVE_ABI_VERSION ||
            h->map->struct_size != (uint32_t)sizeof(DtsxLiveParams)) {
            munmap(mem, DTSX_LIVE_MAP_BYTES);
            close(fd);
            free(h);
            return NULL;
        }
    }
    return h;
}

void dtsx_live_close(DtsxLiveHandle *h)
{
    if (!h) return;
    if (h->map) munmap(h->map, h->map_bytes);
    if (h->fd >= 0) close(h->fd);
    free(h);
}

/* Test helper: remove the underlying shared-memory object. */
void dtsx_live_unlink_label(const char *label)
{
    char name[256];
    if (make_name(label, name, sizeof name) == 0) shm_unlink(name);
}

int dtsx_live_read(DtsxLiveHandle *h, DtsxLiveParams *out, uint32_t *last_seq)
{
    if (!h || !h->map || !out) return 0;
    for (int attempt = 0; attempt < 4; attempt++) {
        const uint32_t s1 = __atomic_load_n(&h->map->seq, __ATOMIC_ACQUIRE);
        if (s1 & 1u) continue;
        if (s1 == 0) return 0;
        if (last_seq && *last_seq == s1) return 0;
        memcpy(out, (const void *)h->map, sizeof(*out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t s2 = __atomic_load_n(&h->map->seq, __ATOMIC_ACQUIRE);
        if (s1 != s2) continue;
        if (out->magic != DTSX_LIVE_MAGIC ||
            out->abi_version != DTSX_LIVE_ABI_VERSION ||
            out->struct_size != (uint32_t)sizeof(DtsxLiveParams)) {
            return 0;
        }
        if (!(out->flags & 1u)) return 0;
        if (last_seq) *last_seq = s2;
        return 1;
    }
    return 0;
}

int dtsx_live_publish(DtsxLiveHandle *h, const DtsxLiveParams *p)
{
    if (!h || !h->map || !p) return -1;
    DtsxLiveParams tmp;
    memcpy(&tmp, p, sizeof(tmp));
    tmp.magic = DTSX_LIVE_MAGIC;
    tmp.abi_version = DTSX_LIVE_ABI_VERSION;
    tmp.struct_size = (uint32_t)sizeof(tmp);
    tmp.flags |= 1u;
    tmp.capabilities = DTSX_LIVE_CAPABILITIES;

    const uint32_t s = __atomic_load_n(&h->map->seq, __ATOMIC_RELAXED);
    const uint32_t odd = (s | 1u);
    __atomic_store_n(&h->map->seq, odd, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    /* Copy every field except the leading sequence counter. */
    memcpy((char *)h->map + sizeof(uint32_t), (const char *)&tmp + sizeof(uint32_t),
           sizeof(tmp) - sizeof(uint32_t));
    __atomic_store_n(&h->map->seq, odd + 1u, __ATOMIC_RELEASE);
    return 0;
}

static double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

void dtsx_live_params_from_config(const DtsxConfig *c, DtsxLiveParams *p)
{
    if (!c || !p) return;
    memset(p, 0, sizeof(*p));
    p->magic = DTSX_LIVE_MAGIC;
    p->abi_version = DTSX_LIVE_ABI_VERSION;
    p->struct_size = (uint32_t)sizeof(*p);
    p->capabilities = DTSX_LIVE_CAPABILITIES;
    p->bypass = c->bypass ? 1 : 0;
    p->hpf_enable = c->hpf_enable ? 1 : 0;
    p->hpf_order = c->hpf_order;
    p->eq_enable = c->eq_enable ? 1 : 0;
    p->eq_n = c->eq_n;
    p->user_eq_enable = c->user_eq_enable ? 1 : 0;
    p->bass_enable = c->bass_enable ? 1 : 0;
    p->mbhl_enable = c->mbhl_enable ? 1 : 0;
    p->loud_enable = c->loud_enable ? 1 : 0;
    for (int i = 0; i < DTSX_MAX_BANDS; i++) {
        if (i < c->eq_n) {
            p->eq_type[i] = c->eq[i].type;
            p->eq_f[i] = (float)c->eq[i].f;
            p->eq_q[i] = (float)c->eq[i].q;
            p->eq_g[i] = (float)c->eq[i].gain;
        }
    }
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++) {
        p->user_eq_type[i] = DTSX_BQ_PEAK;
        p->user_eq_f[i] = (float)dtsx_user_eq_freqs[i];
        p->user_eq_q[i] = 1.0f;
        p->user_eq_g[i] = (float)c->user_eq_g[i];
    }
    p->input_gain_db = (float)c->input_gain_db;
    p->output_gain_db = (float)c->output_gain_db;
    p->hpf_hz = (float)c->hpf_hz;
    p->bass_amount = (float)clampd(c->bass_amount, 0.0, DTSX_BASS_AMOUNT_MAX);
    p->bass_drive = (float)c->bass_drive;
    p->bass_lo_hz = (float)c->bass_lo_hz;
    p->bass_harm_hp_hz = (float)c->bass_harm_hp_hz;
    p->bass_extend_db = (float)c->bass_extend_db;
    p->bass_size_hz = (float)c->bass_size_hz;
    p->bass_harm_mix = (float)c->bass_harm_mix;
    p->mbhl_low_x = (float)c->mbhl_low_x;
    p->mbhl_mid_x = (float)c->mbhl_mid_x;
    p->mbhl_attack_ms = (float)c->mbhl_attack_ms;
    p->mbhl_low_decay_ms = (float)c->mbhl_low_decay_ms;
    p->mbhl_mid_decay_ms = (float)c->mbhl_mid_decay_ms;
    p->mbhl_high_decay_ms = (float)c->mbhl_high_decay_ms;
    p->mbhl_low_ratio = (float)c->mbhl_low_ratio;
    p->mbhl_mid_ratio = (float)c->mbhl_mid_ratio;
    p->mbhl_high_ratio = (float)c->mbhl_high_ratio;
    p->mbhl_low_thresh_db = (float)c->mbhl_low_thresh_db;
    p->mbhl_mid_thresh_db = (float)c->mbhl_mid_thresh_db;
    p->mbhl_high_thresh_db = (float)c->mbhl_high_thresh_db;
    p->mbhl_low_makeup_db = (float)c->mbhl_low_makeup_db;
    p->mbhl_mid_makeup_db = (float)c->mbhl_mid_makeup_db;
    p->mbhl_high_makeup_db = (float)c->mbhl_high_makeup_db;
    p->mbhl_boost_db = (float)c->mbhl_boost_db;
    p->loud_amount = (float)c->loud_amount;
    p->loud_volume = (float)c->loud_volume;
    p->width = (float)c->width;
    p->crossfeed = (float)c->crossfeed;
    p->ceiling_db = (float)c->ceiling_db;
    p->limiter_attack_ms = (float)c->limiter_attack_ms;
    p->limiter_release_ms = (float)c->limiter_release_ms;
    p->lookahead_ms = (float)c->lookahead_ms;
}

int dtsx_live_params_to_config(const DtsxLiveParams *p, DtsxConfig *c)
{
    if (!p || !c) return -1;
    if (p->magic != DTSX_LIVE_MAGIC ||
        p->abi_version != DTSX_LIVE_ABI_VERSION ||
        p->struct_size != (uint32_t)sizeof(*p)) {
        return -1;
    }
    const double fs = c->fs > 0.0 ? c->fs : 48000.0;
    const double nyq = 0.45 * fs;

    c->bypass = p->bypass ? 1 : 0;
    c->hpf_enable = p->hpf_enable ? 1 : 0;
    c->hpf_order = (p->hpf_order >= 4) ? 4 : 2;
    c->hpf_hz = clampd(p->hpf_hz, 5.0, nyq);
    c->eq_enable = p->eq_enable ? 1 : 0;
    c->eq_n = clampi(p->eq_n, 0, DTSX_MAX_BANDS);
    for (int i = 0; i < c->eq_n; i++) {
        c->eq[i].type = clampi(p->eq_type[i], 0, DTSX_BQ_ALLPASS);
        c->eq[i].f = clampd(p->eq_f[i] > 0.0f ? p->eq_f[i] : 1000.0, 5.0, nyq);
        c->eq[i].q = clampd(p->eq_q[i] > 0.0f ? p->eq_q[i] : 0.7071, 0.05, 40.0);
        c->eq[i].gain = clampd(p->eq_g[i], -36.0, 36.0);
    }
    c->user_eq_enable = p->user_eq_enable ? 1 : 0;
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++)
        c->user_eq_g[i] = clampd(p->user_eq_g[i], -12.0, 12.0);

    c->input_gain_db = clampd(p->input_gain_db, -36.0, 24.0);
    c->output_gain_db = clampd(p->output_gain_db, -36.0, 24.0);

    c->bass_enable = p->bass_enable ? 1 : 0;
    c->bass_amount = clampd(p->bass_amount, 0.0, DTSX_BASS_AMOUNT_MAX);
    c->bass_drive = clampd(p->bass_drive, 0.5, 4.0);
    c->bass_lo_hz = clampd(p->bass_lo_hz, 20.0, 400.0);
    c->bass_harm_hp_hz = clampd(p->bass_harm_hp_hz, 10.0, 300.0);
    c->bass_extend_db = clampd(p->bass_extend_db, 0.0, 12.0);
    c->bass_size_hz = clampd(p->bass_size_hz, 20.0, c->bass_lo_hz);
    c->bass_harm_mix = clampd(p->bass_harm_mix, 0.0, 1.0);

    c->mbhl_enable = p->mbhl_enable ? 1 : 0;
    c->mbhl_low_x = clampd(p->mbhl_low_x, 40.0, nyq);
    c->mbhl_mid_x = clampd(p->mbhl_mid_x, c->mbhl_low_x + 1.0, nyq);
    c->mbhl_attack_ms = clampd(p->mbhl_attack_ms, 0.1, 100.0);
    c->mbhl_low_decay_ms = clampd(p->mbhl_low_decay_ms, 10.0, 2000.0);
    c->mbhl_mid_decay_ms = clampd(p->mbhl_mid_decay_ms, 10.0, 2000.0);
    c->mbhl_high_decay_ms = clampd(p->mbhl_high_decay_ms, 10.0, 2000.0);
    c->mbhl_low_ratio = clampd(p->mbhl_low_ratio, 1.0, 20.0);
    c->mbhl_mid_ratio = clampd(p->mbhl_mid_ratio, 1.0, 20.0);
    c->mbhl_high_ratio = clampd(p->mbhl_high_ratio, 1.0, 20.0);
    c->mbhl_low_thresh_db = clampd(p->mbhl_low_thresh_db, -96.0, 0.0);
    c->mbhl_mid_thresh_db = clampd(p->mbhl_mid_thresh_db, -96.0, 0.0);
    c->mbhl_high_thresh_db = clampd(p->mbhl_high_thresh_db, -96.0, 0.0);
    c->mbhl_low_makeup_db = clampd(p->mbhl_low_makeup_db, 0.0, 24.0);
    c->mbhl_mid_makeup_db = clampd(p->mbhl_mid_makeup_db, 0.0, 24.0);
    c->mbhl_high_makeup_db = clampd(p->mbhl_high_makeup_db, 0.0, 24.0);
    c->mbhl_boost_db = clampd(p->mbhl_boost_db, 0.0, 12.0);

    c->loud_enable = p->loud_enable ? 1 : 0;
    c->loud_amount = clampd(p->loud_amount, 0.0, 1.0);
    c->loud_volume = clampd(p->loud_volume, 0.0, 1.0);
    c->width = clampd(p->width, 0.0, 2.0);
    c->crossfeed = clampd(p->crossfeed, 0.0, 1.0);
    c->ceiling_db = clampd(p->ceiling_db, -60.0, -0.01);

    /* Reload-only fields stay at the values the chain was created with. */
    return 0;
}
