#!/usr/bin/env python3
"""Measurement + PipeWire config generation for dtsdsp (no external deps beyond stdlib/numpy/optional matplotlib)."""
import json, math, os, struct, subprocess, sys, wave

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BIN = os.path.join(ROOT, "build", "dtsdsp")

def write_wav(path, fs, samples):
    with wave.open(path, "wb") as w:
        w.setnchannels(1); w.setsampwidth(2); w.setframerate(fs)
        w.writeframes(b"".join(struct.pack("<h", max(-32768, min(32767, int(s * 32767)))) for s in samples))

def read_wav(path):
    with wave.open(path, "rb") as w:
        n = w.getnframes(); ch = w.getnchannels()
        data = w.readframes(n)
    v = struct.unpack("<%dh" % (len(data)//2), data)
    return [x/32768.0 for x in v[::ch]], w.getframerate()

def render(profile, src, dst, fs):
    cfg = os.path.join(ROOT, "configs", profile)
    subprocess.run([BIN, "render", "-p", cfg, "-i", src, "-o", dst], check=True)

def measure(profile, fs=48000):
    freqs = [40, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000]
    rows = []
    for f in freqs:
        n = fs // 2
        x = [0.25 * math.sin(2 * math.pi * f * i / fs) for i in range(n)]
        src = os.path.join(ROOT, "measurements", "_in.wav")
        dst = os.path.join(ROOT, "measurements", "_out.wav")
        write_wav(src, fs, x)
        render(profile, src, dst, fs)
        y, _ = read_wav(dst)
        xr = math.sqrt(sum(v*v for v in x[n//2:]) / (n - n//2)) or 1e-9
        yr = math.sqrt(sum(v*v for v in y[n//2:]) / (n - n//2))
        rows.append((f, 20 * math.log10(max(yr, 1e-9) / xr)))
    return rows

def fr_csv(rows, path):
    with open(path, "w") as fh:
        fh.write("freq_hz,gain_db\n")
        for f, g in rows:
            fh.write("%d,%.3f\n" % (f, g))

def plot(rows, path, title):
    try:
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
    except Exception:
        with open(path.replace(".png", ".txt"), "w") as fh:
            fh.write(title + "\n")
            for f, g in rows:
                fh.write("%6d Hz %+7.2f dB %s\n" % (f, g, "#" * max(0, int((g + 12) * 2))))
        return False
    xs = [f for f, _ in rows]; ys = [g for _, g in rows]
    plt.figure(figsize=(7, 3.6)); plt.semilogx(xs, ys, marker="o")
    plt.grid(True, which="both", alpha=0.3); plt.xlabel("Hz"); plt.ylabel("dB")
    plt.title(title); plt.tight_layout(); plt.savefig(path, dpi=120); plt.close()
    return True

def filterchain(profile, fs=48000):
    cfg = json.load(open(os.path.join(ROOT, "configs", profile)))
    nodes = ["    { type = builtin name = copy_in label = copy }"]
    prev = "copy_in"
    def add(kind, params):
        nonlocal prev, nodes
        name = "n%d" % len(nodes)
        p = " ".join("%s = %s" % (k, v) for k, v in params.items())
        nodes.append("    { type = builtin name = %s label = %s %s }" % (name, kind, p))
        nodes.append("    { type = link name = l%d input = %s:%s output = %s:In }" % (len(nodes), prev, "Out", name))
        prev = name
    add("bq_highpass", {"freq": cfg["hpf_hz"], "Q": cfg["hpf_q"]})
    for f, q, g in zip(cfg["eq_f"], cfg["eq_q"], cfg["eq_g"]):
        kind = "bq_peaking" if abs(g) > 0.05 else None
        if kind: add(kind, {"freq": f, "Q": q, "gain": g})
    return "\n".join(nodes)

if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else "all"
    if cmd in ("all", "measure"):
        for prof, title in (("hp-victus-speakers.json", "Speakers (full chain)"),
                            ("hp-victus-headphones.json", "Headphones (HPEQ-inspired)")):
            rows = measure(prof)
            name = prof.replace(".json", "")
            fr_csv(rows, os.path.join(ROOT, "measurements", "fr_%s.csv" % name))
            ok = plot(rows, os.path.join(ROOT, "measurements", "fr_%s.png" % name), title)
            print("measured", name, "plot" if ok else "ascii", "->", ", ".join("%d:%+.1f" % r for r in rows[:6]))
    if cmd in ("all", "chain"):
        for prof in ("hp-victus-speakers.json", "hp-victus-headphones.json"):
            open(os.path.join(ROOT, "build", "filterchain-" + prof.replace(".json", "") + ".conf"), "w").write(filterchain(prof))
        print("filter-chain configs written to build/")
