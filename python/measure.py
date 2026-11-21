#!/usr/bin/env python3
"""Objective validation of the dtsx chain.

Produces measurements/validation.md, CSVs and SVG figures:
  * linear frequency response (measured vs designed) at 44.1 and 48 kHz
  * full-chain response at -20 dBFS
  * impulse latency
  * THD at 1 kHz
  * limiter ceiling under +12 dB overdrive
  * pink-noise peak/RMS and third-octave spectrum
  * CPU benchmark

Everything here is internal DSP validation. It is NOT a Windows reference
capture and makes no claim of bit-exact reproduction.
"""
import json
import math
import os
import struct
import subprocess
import sys
import wave

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gpeq
import svgplot

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "dtsdsp")
MEAS = os.path.join(ROOT, "measurements")
BUILD = os.path.join(ROOT, "build")
CONF = os.path.join(ROOT, "configs")
FS48 = 48000


# --------------------------------------------------------------------- audio
def write_wav(path, fs, x):
    x = np.asarray(x, dtype=np.float64)
    if x.ndim == 1:
        x = np.stack([x, x], axis=1)
    pcm = np.clip(np.rint(x * 32767.0), -32768, 32767).astype("<i2")
    with wave.open(path, "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(fs)
        w.writeframes(pcm.tobytes())


def read_wav(path):
    with wave.open(path, "rb") as w:
        fs = w.getframerate()
        ch = w.getnchannels()
        n = w.getnframes()
        raw = w.readframes(n)
    v = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
    return fs, v.reshape(-1, ch)


def render(cfg, src, dst):
    subprocess.run([BIN, "render", "-p", cfg, "-i", src, "-o", dst],
                   check=True, stdout=subprocess.DEVNULL)


def tone(fs, f, seconds, level_db):
    n = int(fs * seconds)
    t = np.arange(n) / fs
    a = 10.0 ** (level_db / 20.0)
    x = a * np.sin(2 * math.pi * f * t)
    fade = int(0.03 * fs)
    if fade > 1:
        env = np.ones(n)
        env[:fade] = np.linspace(0, 1, fade)
        env[-fade:] = np.linspace(1, 0, fade)
        x *= env
    return x


# ----------------------------------------------------------------- prediction
def hpf_db(f, fs, f0, order=2):
    w0 = 2 * math.pi * f0 / fs
    cw, sw = math.cos(w0), math.sin(w0)
    alpha = sw / (2 * 0.7071067811865476)
    b0 = (1 + cw) / 2
    b1 = -(1 + cw)
    b2 = b0
    a0 = 1 + alpha
    a1 = -2 * cw
    a2 = 1 - alpha
    stages = 1 if order < 4 else 2
    w = 2 * math.pi * f / fs
    c1, s1 = math.cos(w), math.sin(w)
    c2, s2 = math.cos(2 * w), math.sin(2 * w)
    nr = b0 + b1 * c1 + b2 * c2
    ni = -(b1 * s1 + b2 * s2)
    dr = a0 + a1 * c1 + a2 * c2
    di = -(a1 * s1 + a2 * s2)
    g = 10 * math.log10((nr * nr + ni * ni + 1e-30) / (dr * dr + di * di + 1e-30))
    return g * stages


def predicted_db(cfg, fs, freqs):
    out = []
    for f in freqs:
        g = 0.0
        if cfg.get("hpf_enable"):
            g += hpf_db(f, fs, cfg["hpf_hz"], cfg.get("hpf_order", 2))
        if cfg.get("eq_enable"):
            for t, fc, q, gg in zip(cfg.get("eq_type", []), cfg.get("eq_f", []),
                                    cfg.get("eq_q", []), cfg.get("eq_g", [])):
                if t == 0:
                    g += gpeq.peaking_db(f, fs, fc, q, gg)
        out.append(g)
    return np.array(out)


# --------------------------------------------------------------------- tests
def linear_config(fs):
    c = json.load(open(os.path.join(CONF, "speakers-music.json")))
    c["bass_enable"] = 0
    c["mbhl_enable"] = 0
    c["loud_enable"] = 0
    c["width"] = 1.0
    c["input_gain_db"] = 0.0
    c["output_gain_db"] = 0.0
    c["ceiling_db"] = -0.1
    return c


def measure_fr(cfg, cfgpath, fs, freqs, level_db=-20.0):
    rows = []
    for f in freqs:
        x = tone(fs, f, 0.45, level_db)
        src = os.path.join(BUILD, "_meas_in.wav")
        dst = os.path.join(BUILD, "_meas_out.wav")
        write_wav(src, fs, x)
        render(cfgpath, src, dst)
        _, y = read_wav(dst)
        n = len(x)
        a = int(0.15 * fs)
        b = int(0.35 * fs)
        xr = np.sqrt(np.mean(x[a:b] ** 2))
        yr = np.sqrt(np.mean(y[a:b, 0] ** 2))
        rows.append((f, 20 * math.log10((yr + 1e-12) / (xr + 1e-12))))
    return rows


def impulse_latency(cfgpath, fs):
    n = fs // 2
    x = np.zeros(n)
    x[1000] = 0.5
    src = os.path.join(BUILD, "_imp_in.wav")
    dst = os.path.join(BUILD, "_imp_out.wav")
    write_wav(src, fs, x)
    render(cfgpath, src, dst)
    _, y = read_wav(dst)
    return int(np.argmax(np.abs(y[:, 0]))) - 1000


def thd_1k(cfgpath, fs, level_db=-6.0):
    x = tone(fs, 1000.0, 1.0, level_db)
    src = os.path.join(BUILD, "_thd_in.wav")
    dst = os.path.join(BUILD, "_thd_out.wav")
    write_wav(src, fs, x)
    render(cfgpath, src, dst)
    _, y = read_wav(dst)
    seg = y[int(0.2 * fs):int(0.8 * fs), 0]
    win = np.blackman(len(seg))
    spec = np.abs(np.fft.rfft(seg * win))
    fr = np.fft.rfftfreq(len(seg), 1.0 / fs)
    f0 = np.argmin(np.abs(fr - 1000))
    fund = spec[f0 - 2:f0 + 3].max()
    acc = 0.0
    for k in range(2, 11):
        i = np.argmin(np.abs(fr - 1000 * k))
        acc += spec[i - 2:i + 3].max() ** 2
    return 100.0 * math.sqrt(acc) / (fund + 1e-12)


def limiter_test(cfg, fs):
    c = dict(cfg)
    c["output_gain_db"] = 12.0
    c["ceiling_db"] = -1.0
    path = os.path.join(BUILD, "_limit_cfg.json")
    json.dump(c, open(path, "w"))
    x = tone(fs, 200.0, 1.0, -6.0)
    src = os.path.join(BUILD, "_lim_in.wav")
    dst = os.path.join(BUILD, "_lim_out.wav")
    write_wav(src, fs, x)
    render(path, src, dst)
    _, y = read_wav(dst)
    return float(np.max(np.abs(y))), 20 * math.log10(float(np.max(np.abs(y))) + 1e-12)


def pink_noise(fs, seconds, level_db):
    n = int(fs * seconds)
    rng = np.random.default_rng(1234)
    spec = np.fft.rfft(rng.standard_normal(n))
    f = np.fft.rfftfreq(n, 1.0 / fs)
    f[0] = f[1]
    spec /= np.sqrt(f)
    x = np.fft.irfft(spec, n)
    x /= np.max(np.abs(x)) + 1e-12
    x *= 10 ** (level_db / 20.0)
    return x


def third_octave(y, fs):
    centers = [25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400,
               500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
               6300, 8000, 10000, 12500, 16000]
    n = len(y)
    win = np.hanning(n)
    spec = np.abs(np.fft.rfft(y * win)) ** 2
    fr = np.fft.rfftfreq(n, 1.0 / fs)
    out = []
    for c in centers:
        lo, hi = c / 2 ** (1 / 6), c * 2 ** (1 / 6)
        m = (fr >= lo) & (fr < hi)
        if m.any():
            out.append((c, 10 * math.log10(np.sum(spec[m]) + 1e-30)))
    return out


def main():
    os.makedirs(MEAS, exist_ok=True)
    os.makedirs(BUILD, exist_ok=True)
    freqs = [20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400,
             500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000,
             6300, 8000, 10000, 12500, 16000, 20000]
    report = ["# Validation report (generated)", "",
              "Internal DSP validation only; no Windows reference capture exists",
              "in this workspace. Figures compare the C implementation against the",
              "designed/decoded curves, not against a proprietary runtime.", ""]

    # ---- linear response at 48k and 44.1k
    lin = linear_config(FS48)
    linpath = os.path.join(BUILD, "validation-linear.json")
    json.dump(lin, open(linpath, "w"))
    for fs in (48000, 44100):
        use = [f for f in freqs if f < fs / 2]
        rows = measure_fr(lin, linpath, fs, use)
        pred = predicted_db(lin, fs, use)
        err = np.array([r[1] for r in rows]) - pred
        rms = float(np.sqrt(np.mean(err ** 2)))
        mx = float(np.max(np.abs(err)))
        report.append("## Linear response, %d Hz" % fs)
        report.append("")
        report.append("Measured vs designed (HPF + 19-band EQ): RMS error **%.3f dB**, "
                      "max **%.3f dB**." % (rms, mx))
        report.append("")
        with open(os.path.join(MEAS, "validation-linear-%d.csv" % fs), "w") as fh:
            fh.write("freq_hz,measured_db,predicted_db,error_db\n")
            for (f, g), p, e in zip(rows, pred, err):
                fh.write("%.1f,%.3f,%.3f,%.3f\n" % (f, g, p, e))
        svgplot.response_svg(
            os.path.join(MEAS, "validation-linear-%d.svg" % fs),
            "Linear chain response vs design (%d Hz)" % fs,
            [("measured", use, [r[1] for r in rows], "#c8341f", False),
             ("designed", use, list(pred), "#1f4ec8", True)],
            dmin=-30, dmax=20)

    # ---- latency
    lat = impulse_latency(linpath, FS48)
    report += ["## Latency", "",
               "Impulse peak offset through the linear chain: **%d samples** "
               "(%.2f ms at 48 kHz). This equals the limiter lookahead."
               % (lat, 1000.0 * lat / FS48), ""]

    # ---- full chain response
    spk = json.load(open(os.path.join(CONF, "speakers-music.json")))
    spkpath = os.path.join(CONF, "speakers-music.json")
    use = freqs
    rows_full = measure_fr(spk, spkpath, FS48, use, level_db=-20.0)
    with open(os.path.join(MEAS, "validation-fullchain.csv"), "w") as fh:
        fh.write("freq_hz,full_chain_db\n")
        for f, g in rows_full:
            fh.write("%.1f,%.3f\n" % (f, g))
    svgplot.response_svg(
        os.path.join(MEAS, "validation-fullchain.svg"),
        "Full speaker chain at -20 dBFS (48 kHz, non-linear stage active)",
        [("full chain", use, [r[1] for r in rows_full], "#7a1fa2", False)],
        dmin=-40, dmax=20)
    report += ["## Full-chain response", "",
               "Measured at -20 dBFS; the harmonic bass stage is level-independent "
               "in its mixing amount but nonlinear, so this is not a linear transfer "
               "function (see validation-fullchain.svg).", ""]

    # ---- THD
    thd = thd_1k(spkpath, FS48, -6.0)
    report += ["## Distortion", "",
               "THD (harmonics 2..10) at 1 kHz, -6 dBFS: **%.3f %%** (%.1f dB)."
               % (thd, 20 * math.log10(thd / 100.0 + 1e-12)), ""]

    # ---- limiter
    peak, peak_db = limiter_test(spk, FS48)
    report += ["## Limiter", "",
               "With +12 dB output gain and a -6 dBFS 200 Hz tone, output peak is "
               "**%.4f** (**%.2f dBFS**) against a -1.0 dBFS ceiling."
               % (peak, peak_db), ""]

    # ---- pink noise
    fs = FS48
    x = pink_noise(fs, 5.0, -18.0)
    src = os.path.join(BUILD, "_pink_in.wav")
    dst = os.path.join(BUILD, "_pink_out.wav")
    write_wav(src, fs, x)
    render(spkpath, src, dst)
    _, y = read_wav(dst)
    peak = float(np.max(np.abs(y)))
    rms = float(np.sqrt(np.mean(y ** 2)))
    report += ["## Pink noise", "",
               "5 s pink noise at -18 dBFS: output peak **%.4f** (%.2f dBFS), "
               "RMS **%.4f** (%.2f dBFS)." % (peak, 20 * math.log10(peak),
                                               rms, 20 * math.log10(rms)), ""]
    bands = third_octave(y[:, 0], fs)
    svgplot.response_svg(
        os.path.join(MEAS, "validation-spectrum.svg"),
        "Output third-octave spectrum, pink noise at -18 dBFS",
        [("output", [b[0] for b in bands],
          [b[1] - max(v for _, v in bands) for b in bands], "#0a7a55", False)],
        dmin=-45, dmax=5)
    with open(os.path.join(MEAS, "validation-spectrum.csv"), "w") as fh:
        fh.write("freq_hz,relative_db\n")
        top = max(v for _, v in bands)
        for c, v in bands:
            fh.write("%.1f,%.3f\n" % (c, v - top))

    # ---- CPU
    bench = subprocess.run([BIN, "bench", "-p", spkpath], capture_output=True,
                           text=True)
    report += ["## CPU", "", "```", bench.stdout.strip(), "```", ""]

    report += ["## Files", "",
               "CSV and SVG files in `measurements/` are generated by "
               "`python3 python/measure.py`.", ""]
    open(os.path.join(MEAS, "validation.md"), "w").write("\n".join(report))
    print("\n".join(report))


if __name__ == "__main__":
    main()
