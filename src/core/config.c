/* Minimal flat-JSON configuration reader/writer.
 *
 * The schema is deliberately flat: numbers, booleans (0/1) and numeric arrays.
 * This keeps the real-time side free of any parser and makes user profiles
 * easy to inspect and diff.
 */
#include "dtsx.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

const double dtsx_user_eq_freqs[DTSX_USER_EQ_BANDS] = {
    31.5, 63.0, 125.0, 250.0, 500.0, 1000.0, 2000.0, 4000.0, 8000.0, 16000.0
};

static const char *find_key(const char *js, const char *key)
{
    char pat[80];
    snprintf(pat, sizeof pat, "\"%s\"", key);
    const char *p = strstr(js, pat);
    if (!p) return NULL;
    p += strlen(pat);
    while (*p && *p != ':') p++;
    if (!*p) return NULL;
    p++;
    while (*p && isspace((unsigned char)*p)) p++;
    return p;
}

static double get_num(const char *js, const char *key, double dflt)
{
    const char *p = find_key(js, key);
    if (!p) return dflt;
    char *end = NULL;
    double v = strtod(p, &end);
    return (end == p) ? dflt : v;
}

static int get_int(const char *js, const char *key, int dflt)
{
    return (int)lrint(get_num(js, key, (double)dflt));
}

static int get_arr(const char *js, const char *key, double *out, int max)
{
    const char *p = find_key(js, key);
    if (!p || *p != '[') return 0;
    p++;
    int n = 0;
    while (n < max) {
        while (*p && (isspace((unsigned char)*p) || *p == ',')) p++;
        if (*p == ']' || !*p) break;
        char *end = NULL;
        double v = strtod(p, &end);
        if (end == p) break;
        out[n++] = v;
        p = end;
    }
    return n;
}

int dtsx_config_defaults(DtsxConfig *c, double fs)
{
    memset(c, 0, sizeof(*c));
    c->fs = fs > 0 ? fs : 48000.0;
    c->input_gain_db = 0.0;
    c->output_gain_db = 0.0;
    c->hpf_enable = 1;
    c->hpf_hz = 100.0;
    c->hpf_order = 2;
    c->eq_enable = 0;
    c->eq_n = 0;
    c->user_eq_enable = 0;
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++) c->user_eq_g[i] = 0.0;
    c->bass_enable = 0;
    c->bass_amount = 0.0;
    c->bass_drive = 1.2;
    c->bass_lo_hz = 150.0;
    c->bass_harm_hp_hz = 60.0;
    c->bass_extend_db = 0.0;
    c->bass_size_hz = 55.0;
    c->bass_harm_mix = 1.0;
    c->mbhl_enable = 0;
    c->mbhl_low_x = 440.0;
    c->mbhl_mid_x = 2000.0;
    c->mbhl_attack_ms = 5.0;
    c->mbhl_low_decay_ms = 500.0;
    c->mbhl_mid_decay_ms = 500.0;
    c->mbhl_high_decay_ms = 250.0;
    c->mbhl_low_ratio = 6.0;
    c->mbhl_mid_ratio = 8.0;
    c->mbhl_high_ratio = 4.0;
    c->mbhl_low_thresh_db = -12.0;
    c->mbhl_mid_thresh_db = -16.0;
    c->mbhl_high_thresh_db = -6.0;
    c->mbhl_low_makeup_db = 3.0;
    c->mbhl_mid_makeup_db = 0.0;
    c->mbhl_high_makeup_db = 3.0;
    c->mbhl_boost_db = 0.0;
    c->loud_enable = 0;
    c->loud_amount = 0.6;
    c->loud_volume = 0.5;
    c->width = 1.0;
    c->crossfeed = 0.0;
    c->ceiling_db = -1.0;
    c->limiter_attack_ms = 2.0;
    c->limiter_release_ms = 120.0;
    c->lookahead_ms = 5.0;
    c->bypass = 0;
    return 0;
}

int dtsx_config_load(DtsxConfig *c, const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return -1; }
    long n = ftell(f);
    if (n <= 0 || n > 1 << 20) { fclose(f); return -1; }
    rewind(f);
    char *js = (char *)malloc((size_t)n + 1);
    if (!js) { fclose(f); return -1; }
    if (fread(js, 1, (size_t)n, f) != (size_t)n) { free(js); fclose(f); return -1; }
    js[n] = 0;
    fclose(f);

    c->fs = get_num(js, "fs", c->fs);
    c->input_gain_db = get_num(js, "input_gain_db", c->input_gain_db);
    c->output_gain_db = get_num(js, "output_gain_db", c->output_gain_db);
    c->hpf_enable = get_int(js, "hpf_enable", c->hpf_enable);
    c->hpf_hz = get_num(js, "hpf_hz", c->hpf_hz);
    c->hpf_order = get_int(js, "hpf_order", c->hpf_order);
    c->eq_enable = get_int(js, "eq_enable", c->eq_enable);
    c->user_eq_enable = get_int(js, "user_eq_enable", c->user_eq_enable);

    c->bass_enable = get_int(js, "bass_enable", c->bass_enable);
    c->bass_amount = get_num(js, "bass_amount", c->bass_amount);
    c->bass_drive = get_num(js, "bass_drive", c->bass_drive);
    c->bass_lo_hz = get_num(js, "bass_lo_hz", c->bass_lo_hz);
    c->bass_harm_hp_hz = get_num(js, "bass_harm_hp_hz", c->bass_harm_hp_hz);
    c->bass_extend_db = get_num(js, "bass_extend_db", c->bass_extend_db);
    c->bass_size_hz = get_num(js, "bass_size_hz", c->bass_size_hz);
    c->bass_harm_mix = get_num(js, "bass_harm_mix", c->bass_harm_mix);

    c->mbhl_enable = get_int(js, "mbhl_enable", c->mbhl_enable);
    c->mbhl_low_x = get_num(js, "mbhl_low_x", c->mbhl_low_x);
    c->mbhl_mid_x = get_num(js, "mbhl_mid_x", c->mbhl_mid_x);
    c->mbhl_attack_ms = get_num(js, "mbhl_attack_ms", c->mbhl_attack_ms);
    c->mbhl_low_decay_ms = get_num(js, "mbhl_low_decay_ms", c->mbhl_low_decay_ms);
    c->mbhl_mid_decay_ms = get_num(js, "mbhl_mid_decay_ms", c->mbhl_mid_decay_ms);
    c->mbhl_high_decay_ms = get_num(js, "mbhl_high_decay_ms", c->mbhl_high_decay_ms);
    c->mbhl_low_ratio = get_num(js, "mbhl_low_ratio", c->mbhl_low_ratio);
    c->mbhl_mid_ratio = get_num(js, "mbhl_mid_ratio", c->mbhl_mid_ratio);
    c->mbhl_high_ratio = get_num(js, "mbhl_high_ratio", c->mbhl_high_ratio);
    c->mbhl_low_thresh_db = get_num(js, "mbhl_low_thresh_db", c->mbhl_low_thresh_db);
    c->mbhl_mid_thresh_db = get_num(js, "mbhl_mid_thresh_db", c->mbhl_mid_thresh_db);
    c->mbhl_high_thresh_db = get_num(js, "mbhl_high_thresh_db", c->mbhl_high_thresh_db);
    c->mbhl_low_makeup_db = get_num(js, "mbhl_low_makeup_db", c->mbhl_low_makeup_db);
    c->mbhl_mid_makeup_db = get_num(js, "mbhl_mid_makeup_db", c->mbhl_mid_makeup_db);
    c->mbhl_high_makeup_db = get_num(js, "mbhl_high_makeup_db", c->mbhl_high_makeup_db);
    c->mbhl_boost_db = get_num(js, "mbhl_boost_db", c->mbhl_boost_db);

    c->loud_enable = get_int(js, "loud_enable", c->loud_enable);
    c->loud_amount = get_num(js, "loud_amount", c->loud_amount);
    c->loud_volume = get_num(js, "loud_volume", c->loud_volume);
    c->width = get_num(js, "width", c->width);
    c->crossfeed = get_num(js, "crossfeed", c->crossfeed);
    c->ceiling_db = get_num(js, "ceiling_db", c->ceiling_db);
    c->limiter_attack_ms = get_num(js, "limiter_attack_ms", c->limiter_attack_ms);
    c->limiter_release_ms = get_num(js, "limiter_release_ms", c->limiter_release_ms);
    c->lookahead_ms = get_num(js, "lookahead_ms", c->lookahead_ms);
    c->bypass = get_int(js, "bypass", c->bypass);

    double ty[DTSX_MAX_BANDS], fv[DTSX_MAX_BANDS], qv[DTSX_MAX_BANDS], gv[DTSX_MAX_BANDS];
    const int nt = get_arr(js, "eq_type", ty, DTSX_MAX_BANDS);
    const int nf = get_arr(js, "eq_f", fv, DTSX_MAX_BANDS);
    const int nq = get_arr(js, "eq_q", qv, DTSX_MAX_BANDS);
    const int ng = get_arr(js, "eq_g", gv, DTSX_MAX_BANDS);
    int m = nt;
    if (nf < m) m = nf;
    if (nq < m) m = nq;
    if (ng < m) m = ng;
    if (m < 0) m = 0;
    for (int i = 0; i < m; i++) {
        c->eq[i].type = (int)lrint(ty[i]);
        c->eq[i].f = fv[i];
        c->eq[i].q = qv[i] > 0.01 ? qv[i] : 0.7071;
        c->eq[i].gain = gv[i];
    }
    c->eq_n = m;
    c->eq_enable = c->eq_enable && m > 0;

    double ueq[DTSX_USER_EQ_BANDS];
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++) ueq[i] = c->user_eq_g[i];
    const int nu = get_arr(js, "user_eq_g", ueq, DTSX_USER_EQ_BANDS);
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++)
        c->user_eq_g[i] = (i < nu) ? ueq[i] : 0.0;
    c->user_eq_enable = c->user_eq_enable ? 1 : 0;

    free(js);
    return 0;
}

static void wnum(FILE *f, const char *key, double v, int last)
{
    fprintf(f, "  \"%s\": %.10g%s\n", key, v, last ? "" : ",");
}

int dtsx_config_write(const DtsxConfig *c, const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    fprintf(f, "{\n");
    fprintf(f, "  \"name\": \"dtsx profile\",\n");
    wnum(f, "fs", c->fs, 0);
    wnum(f, "input_gain_db", c->input_gain_db, 0);
    wnum(f, "output_gain_db", c->output_gain_db, 0);
    wnum(f, "hpf_enable", c->hpf_enable, 0);
    wnum(f, "hpf_hz", c->hpf_hz, 0);
    wnum(f, "hpf_order", c->hpf_order, 0);
    wnum(f, "eq_enable", c->eq_enable, 0);
    wnum(f, "user_eq_enable", c->user_eq_enable, 0);
    wnum(f, "bass_enable", c->bass_enable, 0);
    wnum(f, "bass_amount", c->bass_amount, 0);
    wnum(f, "bass_drive", c->bass_drive, 0);
    wnum(f, "bass_lo_hz", c->bass_lo_hz, 0);
    wnum(f, "bass_harm_hp_hz", c->bass_harm_hp_hz, 0);
    wnum(f, "bass_extend_db", c->bass_extend_db, 0);
    wnum(f, "bass_size_hz", c->bass_size_hz, 0);
    wnum(f, "bass_harm_mix", c->bass_harm_mix, 0);
    wnum(f, "mbhl_enable", c->mbhl_enable, 0);
    wnum(f, "mbhl_low_x", c->mbhl_low_x, 0);
    wnum(f, "mbhl_mid_x", c->mbhl_mid_x, 0);
    wnum(f, "mbhl_attack_ms", c->mbhl_attack_ms, 0);
    wnum(f, "mbhl_low_decay_ms", c->mbhl_low_decay_ms, 0);
    wnum(f, "mbhl_mid_decay_ms", c->mbhl_mid_decay_ms, 0);
    wnum(f, "mbhl_high_decay_ms", c->mbhl_high_decay_ms, 0);
    wnum(f, "mbhl_low_ratio", c->mbhl_low_ratio, 0);
    wnum(f, "mbhl_mid_ratio", c->mbhl_mid_ratio, 0);
    wnum(f, "mbhl_high_ratio", c->mbhl_high_ratio, 0);
    wnum(f, "mbhl_low_thresh_db", c->mbhl_low_thresh_db, 0);
    wnum(f, "mbhl_mid_thresh_db", c->mbhl_mid_thresh_db, 0);
    wnum(f, "mbhl_high_thresh_db", c->mbhl_high_thresh_db, 0);
    wnum(f, "mbhl_low_makeup_db", c->mbhl_low_makeup_db, 0);
    wnum(f, "mbhl_mid_makeup_db", c->mbhl_mid_makeup_db, 0);
    wnum(f, "mbhl_high_makeup_db", c->mbhl_high_makeup_db, 0);
    wnum(f, "mbhl_boost_db", c->mbhl_boost_db, 0);
    wnum(f, "loud_enable", c->loud_enable, 0);
    wnum(f, "loud_amount", c->loud_amount, 0);
    wnum(f, "loud_volume", c->loud_volume, 0);
    wnum(f, "width", c->width, 0);
    wnum(f, "crossfeed", c->crossfeed, 0);
    wnum(f, "ceiling_db", c->ceiling_db, 0);
    wnum(f, "limiter_attack_ms", c->limiter_attack_ms, 0);
    wnum(f, "limiter_release_ms", c->limiter_release_ms, 0);
    wnum(f, "lookahead_ms", c->lookahead_ms, 0);
    wnum(f, "bypass", c->bypass, 0);

    fprintf(f, "  \"eq_type\": [");
    for (int i = 0; i < c->eq_n; i++) fprintf(f, "%s%d", i ? ", " : "", c->eq[i].type);
    fprintf(f, "],\n  \"eq_f\": [");
    for (int i = 0; i < c->eq_n; i++) fprintf(f, "%s%.6g", i ? ", " : "", c->eq[i].f);
    fprintf(f, "],\n  \"eq_q\": [");
    for (int i = 0; i < c->eq_n; i++) fprintf(f, "%s%.6g", i ? ", " : "", c->eq[i].q);
    fprintf(f, "],\n  \"eq_g\": [");
    for (int i = 0; i < c->eq_n; i++) fprintf(f, "%s%.6g", i ? ", " : "", c->eq[i].gain);
    fprintf(f, "],\n  \"user_eq_g\": [");
    for (int i = 0; i < DTSX_USER_EQ_BANDS; i++)
        fprintf(f, "%s%.6g", i ? ", " : "", c->user_eq_g[i]);
    fprintf(f, "]\n}\n");
    fclose(f);
    return 0;
}

void dtsx_config_print(const DtsxConfig *c, void *file)
{
    FILE *f = (FILE *)file;
    fprintf(f, "fs=%g hpf=%g/%d eq_bands=%d user_eq=%d bass=%.3f mbhl=%d loud=%.2f width=%.3f ceiling=%.1f dB\n",
            c->fs, c->hpf_hz, c->hpf_order, c->eq_n, c->user_eq_enable, c->bass_amount,
            c->mbhl_enable, c->loud_amount, c->width, c->ceiling_db);
}
