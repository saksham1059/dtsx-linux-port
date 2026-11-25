#!/usr/bin/env python3
"""Build dtsx profiles from the OEM configuration and decoded GPEQ responses.

Usage: python3 python/build_configs.py [--write]

Reads evidence/profile/dts_apo4_oem_config_103C8D6C.xml and the GPEQ blobs,
fits an independent 19-band parametric EQ to each mode's recovered response,
and writes configs/*.json plus measurements/gpeq_*.csv|.svg.
"""
import json
import math
import os
import re
import sys

import numpy as np
from scipy.optimize import least_squares

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpeq
import svgplot

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XML = os.path.join(ROOT, "evidence/profile/dts_apo4_oem_config_103C8D6C.xml")
PROF = os.path.join(ROOT, "evidence/profile")
MEAS = os.path.join(ROOT, "measurements")
CONF = os.path.join(ROOT, "configs")
FS = 48000.0

CENTERS = [32, 45, 63, 90, 125, 180, 250, 355, 500, 710, 1000,
           1400, 2000, 2800, 4000, 5600, 8000, 11000, 15000]
Q = 1.35
FIT_LO, FIT_HI = 35.0, 17000.0

SPEAKER_MODES = {
    "music": "APO4-Music",
    "movie": "APO4-Movie",
    "voice": "APO4-Voice",
    "game1": "APO4-Game1",
    "game2": "APO4-Game2",
}


def parse_xml():
    text = open(XML, encoding="utf-8").read()
    blocks = re.findall(r"<operating_mode>(.*?)</operating_mode>", text, re.S)
    out = {}
    for b in blocks:
        d = dict(re.findall(r'<control name="([^"]+)"[^>]*>([^<]*)</control>', b))
        key = (d.get("APO4-Speaker Type", ""), d.get("APO4-Operating Mode", ""))
        out[key] = d
    return out


def fit_eq(target_db, freqs):
    def model(g):
        return np.array(gpeq.eq_model_db(g, CENTERS, Q, FS, freqs))

    tgt = np.asarray(target_db)
    x0 = np.array([float(np.interp(math.log(c), np.log(freqs), tgt))
                   for c in CENTERS])
    x0 = np.clip(x0, -12, 12)

    def res(g):
        return np.concatenate([(model(g) - tgt),
                               0.04 * g])

    sol = least_squares(res, x0, bounds=(-15, 15), max_nfev=400,
                        xtol=1e-10, ftol=1e-10, gtol=1e-10)
    gains = np.round(sol.x, 2)
    err = model(gains) - tgt
    return gains, float(np.sqrt(np.mean(err ** 2))), float(np.max(np.abs(err)))


def build_speaker_configs(write):
    blocks = parse_xml()
    freqs = gpeq.log_grid(20, 20000, 420)
    fit_freqs = gpeq.log_grid(FIT_LO, FIT_HI, 380)
    summary = []
    for slug, mode in SPEAKER_MODES.items():
        key = ("APO4-Internal Stereo", mode)
        d = blocks.get(key)
        if not d:
            print("missing block", key)
            continue
        blob = os.path.join(PROF, "dts_gpeq_%s_48k_103C8D6C.bin" %
                            {"music": "mode1", "movie": "mode2", "voice": "mode3",
                             "game1": "mode4", "game2": "mode5"}[slug])
        (_n, secs), _ = gpeq.decode(blob)
        raw = gpeq.cascade_db(secs, FS, freqs)
        raw_fit = gpeq.cascade_db(secs, FS, fit_freqs)
        ref = gpeq.cascade_db(secs, FS, [1000.0])[0]
        target = [v - ref for v in raw]
        gains, rms, peak = fit_eq([v - ref for v in raw_fit], fit_freqs)
        fitted = gpeq.eq_model_db(gains, CENTERS, Q, FS, freqs)
        e1k = gpeq.eq_model_db(gains, CENTERS, Q, FS, [1000.0])[0]

        baseline = float(d.get("SFX:Eagle-TBHDX Bass Level", "0") or 0)
        extend = float(d.get("SFX:Eagle-TBHDX Extended Bass", "0") or 0)
        size = float(d.get("SFX:Eagle-TBHDX Speaker Custom Size", "280") or 280)
        in_gain = float(d.get("SFX:Eagle-TBHDX Input Gain", "0") or 0) / 10.0
        efx_boost = float(d.get("EFX:Eagle-MBHL Boost", "0") or 0) / 10.0
        hpf_on = int(float(d.get("EFX:Eagle-HPF Enable", "0") or 0))
        hpf_hz = float(d.get("EFX:Eagle-HPF Freq", "100") or 100)
        lc_on = int(float(d.get("SFX:Eagle-LC Enable", "0") or 0))
        stereo = d.get("Eagle-Stereo Mode", "Eagle-Off")
        width = {"Eagle-Wide": 1.12, "Eagle-Front": 1.05}.get(stereo, 1.0)

        cfg = {
            "name": "HP Victus 15 / ALC245 internal speakers (%s)" % mode,
            "evidence_level": "independent reconstruction from OEM profile + decoded GPEQ",
            "fs": 48000,
            "input_gain_db": in_gain,
            "output_gain_db": -3.0,
            "hpf_enable": hpf_on,
            "hpf_hz": hpf_hz,
            "hpf_order": int(float(d.get("EFX:Eagle-HPF Order", "2") or 2)),
            "eq_enable": 1,
            "eq_type": [0] * len(CENTERS),
            "eq_f": CENTERS,
            "eq_q": [Q] * len(CENTERS),
            "eq_g": [float(g) for g in gains],
            "bass_enable": 1 if baseline > 0 else 0,
            "bass_amount": round(min(0.55, baseline / 100.0 * 1.1), 3),
            "bass_drive": round(1.0 + baseline / 150.0, 3),
            "bass_lo_hz": 150.0,
            "bass_harm_hp_hz": 60.0,
            "bass_extend_db": round(extend / 10.0, 2),
            "bass_size_hz": round(55.0 + (300.0 - size) * 0.12, 1),
            "bass_harm_mix": 1.0,
            "mbhl_enable": 1,
            "mbhl_low_x": float(d.get("EFX:Eagle-MBHL Low Cross", "440") or 440),
            "mbhl_mid_x": float(d.get("EFX:Eagle-MBHL Mid Cross", "2000") or 2000),
            "mbhl_attack_ms": float(d.get("EFX:Eagle-MBHL Comp Attack", "5") or 5),
            "mbhl_low_decay_ms": float(d.get("EFX:Eagle-MBHL Comp Low Decay", "500") or 500),
            "mbhl_mid_decay_ms": float(d.get("EFX:Eagle-MBHL Comp Mid Decay", "500") or 500),
            "mbhl_high_decay_ms": float(d.get("EFX:Eagle-MBHL Comp High Decay", "250") or 250),
            "mbhl_low_ratio": float(d.get("EFX:Eagle-MBHL Low Ratio", "6") or 6),
            "mbhl_mid_ratio": float(d.get("EFX:Eagle-MBHL Mid Ratio", "8") or 8),
            "mbhl_high_ratio": float(d.get("EFX:Eagle-MBHL High Ratio", "4") or 4),
            "mbhl_low_thresh_db": float(d.get("EFX:Eagle-MBHL Low Thresh", "-120") or -120) / 10.0,
            "mbhl_mid_thresh_db": float(d.get("EFX:Eagle-MBHL Mid Thresh", "-160") or -160) / 10.0,
            "mbhl_high_thresh_db": float(d.get("EFX:Eagle-MBHL High Thresh", "-60") or -60) / 10.0,
            "mbhl_low_makeup_db": float(d.get("EFX:Eagle-MBHL Low Makeup Gain", "30") or 30) / 10.0,
            "mbhl_mid_makeup_db": float(d.get("EFX:Eagle-MBHL Mid Makeup Gain", "0") or 0) / 10.0,
            "mbhl_high_makeup_db": float(d.get("EFX:Eagle-MBHL High Makeup Gain", "30") or 30) / 10.0,
            "mbhl_boost_db": efx_boost,
            "loud_enable": lc_on,
            "loud_amount": 0.7,
            "loud_volume": 0.55,
            "width": width,
            "crossfeed": 0.0,
            "ceiling_db": -1.0,
            "limiter_attack_ms": 2.0,
            "limiter_release_ms": 120.0,
            "lookahead_ms": 5.0,
            "bypass": 0,
        }
        path = os.path.join(CONF, "speakers-%s.json" % slug)
        if write:
            with open(path, "w") as f:
                json.dump(cfg, f, indent=2)
                f.write("\n")
        summary.append((slug, rms, peak, target, fitted))

        with open(os.path.join(MEAS, "gpeq_%s.csv" % slug), "w") as f:
            f.write("freq_hz,gpeq_target_db,gpeq_fit_db\n")
            for fr, t, m in zip(freqs, target, fitted):
                f.write("%.1f,%.3f,%.3f\n" % (fr, t, m))
        svgplot.response_svg(
            os.path.join(MEAS, "gpeq_%s.svg" % slug),
            "GPEQ %s: decoded OEM response vs independent 19-band fit" % mode,
            [("decoded GPEQ", freqs, target, "#1f4ec8", True),
             ("dtsx fit", freqs, fitted, "#c8341f", False)],
            dmin=-28, dmax=18)
        print("%-6s in-band fit RMS %.2f dB, max %.2f dB, 1k fit %.2f dB"
              % (slug, rms, peak, e1k))
    return summary


def headphone_config(write):
    """Conservative headphone profile.

    HPEQ's binary layout is not decoded; the OEM profile shows the headphone
    path disables HPF, uses HPEQ instead of GPEQ, uses a small TruBass setting
    (bass 12 / extended 10), MBHL boost 30 and I3DA spatialisation. We mirror
    the control intent without copying an unknown filter: a mild low shelf,
    crossfeed, and light dynamics.
    """
    cfg = {
        "name": "HP Victus 15 / ALC245 headphones (HPEQ-inspired, conservative)",
        "evidence_level": "OEM control values; HPEQ blob not decoded - no exact correction claimed",
        "fs": 48000,
        "input_gain_db": -3.0,
        "output_gain_db": -1.5,
        "hpf_enable": 0,
        "hpf_hz": 22.0,
        "hpf_order": 2,
        "eq_enable": 1,
        "eq_type": [1, 0, 0, 4],
        "eq_f": [55.0, 220.0, 2800.0, 9000.0],
        "eq_q": [0.7, 0.9, 1.1, 0.7],
        "eq_g": [1.5, 0.8, -1.0, 0.8],
        "bass_enable": 1,
        "bass_amount": 0.1,
        "bass_drive": 1.05,
        "bass_lo_hz": 120.0,
        "bass_harm_hp_hz": 45.0,
        "bass_extend_db": 1.0,
        "bass_size_hz": 30.0,
        "bass_harm_mix": 0.7,
        "mbhl_enable": 1,
        "mbhl_low_x": 300.0,
        "mbhl_mid_x": 4000.0,
        "mbhl_attack_ms": 5.0,
        "mbhl_low_decay_ms": 500.0,
        "mbhl_mid_decay_ms": 250.0,
        "mbhl_high_decay_ms": 200.0,
        "mbhl_low_ratio": 4.0,
        "mbhl_mid_ratio": 4.0,
        "mbhl_high_ratio": 4.0,
        "mbhl_low_thresh_db": -14.0,
        "mbhl_mid_thresh_db": -18.0,
        "mbhl_high_thresh_db": -10.0,
        "mbhl_low_makeup_db": 0.0,
        "mbhl_mid_makeup_db": 0.0,
        "mbhl_high_makeup_db": 0.0,
        "mbhl_boost_db": 3.0,
        "loud_enable": 0,
        "loud_amount": 0.5,
        "loud_volume": 0.5,
        "width": 1.0,
        "crossfeed": 0.25,
        "ceiling_db": -1.5,
        "limiter_attack_ms": 2.0,
        "limiter_release_ms": 120.0,
        "lookahead_ms": 5.0,
        "bypass": 0,
    }
    if write:
        with open(os.path.join(CONF, "headphones-music.json"), "w") as f:
            json.dump(cfg, f, indent=2)
            f.write("\n")
    print("headphone profile written")
    return cfg


def main():
    write = "--write" in sys.argv
    os.makedirs(MEAS, exist_ok=True)
    os.makedirs(CONF, exist_ok=True)
    build_speaker_configs(write)
    headphone_config(write)
    if not write:
        print("(dry run; pass --write to save configs)")


if __name__ == "__main__":
    main()
