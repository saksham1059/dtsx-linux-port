# DSP design

Goal: reproduce as much of the **audible behaviour** of Windows DTS:X Ultra as
can be justified by evidence, as an independent implementation. This is not a
bit-exact clone and does not link or copy proprietary code or data.

The design is split into a real-time C11 core and offline Python tooling.

---

## 1. Biquads

Transposed direct form II:

```
y[n]  = b0*x[n] + z1[n-1]
z1[n] = b1*x[n] - a1*y[n] + z2[n-1]
z2[n] = b2*x[n] - a2*y[n]
```

Coefficients use the RBJ audio-EQ cookbook (peaking, low/high shelf, Butterworth
LP/HP, notch, allpass) with `a0` normalised out. Coefficients are `double`,
state is `float`; denormals are flushed. This form has good numerical behaviour
for the narrow, high-Q bands present in the decoded speaker curve.

`dtsx_chain_eq_response_db()` evaluates the same coefficients analytically, and
the measurement suite compares it to rendered audio: RMS error 0.012 dB.

## 2. Speaker correction EQ

The shipped profiles use an **independent 19-band peaking EQ** (fixed
ISO-like centres 32 Hz…15 kHz, Q = 1.35). OEM coefficients and private profile
inputs are not included in this repository. The recorded fit residuals are:

* Music: RMS 0.44 dB, max 3.52 dB
* Movie: RMS 0.83 dB, max 5.49 dB
* Voice: RMS 0.67 dB, max 5.77 dB
* Game1/2: RMS 0.72 dB, max 5.98 dB

The low-frequency response is separately protected by the high-pass. The
profile gains are ordinary parametric-EQ settings, not copied binary data.

## 3. Harmonic bass enhancement ("TruBass HDX-inspired")

The OEM control set (`Bass Level`, `Extended Bass`, `HRM Level`, `Dynamics`,
`HP Ratio`, `Speaker Custom Size`) and the public psychoacoustic principle of
the missing fundamental motivate this design. The Windows DSP itself was not
decompiled; this is an independent algorithm, not a clone.

Per channel:

```
low       = LPF(x, bass_lo_hz)
rect      = |low| * drive
rect     -= one-pole DC estimate (12 Hz)
harm      = HPF(rect, bass_harm_hp_hz)
harm      = LPF(harm, 3.5 * bass_lo_hz)          // keep in driver range
harm      = harm / (1 + 0.7|harm|)               // soft saturation
dry       = HPF(low, bass_size_hz)               // excursion protection
dry       = lowshelf(dry, 0.8*bass_lo_hz, bass_extend_db)
y         = x + amount * (0.85*dry + 0.55*harm)
```

Mapping from OEM values:

| OEM control | Implementation |
|---|---|
| Bass Level 0–100 | `bass_amount = min(0.55, 0.011 × level)` |
| Dynamics | `bass_drive = 1 + level/150` |
| Extended Bass 0–100 | `bass_extend_db = level/10` |
| Speaker Custom Size | `bass_size_hz = 55 + (300 − size) × 0.12` |
| HRM Level | not modelled (documented) |
| HP Ratio | not modelled (documented) |

Why harmonics: small laptop drivers cannot produce 40–80 Hz at useful level
without excursion. The ear infers the fundamental from the harmonic series
(missing-fundamental phenomenon; see `research/RESEARCH_LOG.md`). The dry low
band is therefore attenuated at the driver limit and the harmonic product is
mixed in.

## 4. Three-band dynamics ("MBHL-inspired")

Linkwitz-Riley 4th-order split:

```
low  = LP4(x, f_low)                       // 440 Hz (internal speakers)
mid  = LP4(HP4(x, f_low), f_mid)            // 2000 Hz
high = HP4(HP4(x, f_low), f_mid)
```

LR4 low+high sums to an all-pass, so the band sum is magnitude-flat when no
compression is active. Per band:

```
env   = one-pole(|x|, attack, decay)
level = 20 log10(env)
over  = max(0, level − threshold)
gr    = over * (1 − 1/ratio)               // dB
gr   ← one-pole(gr, attack when rising, decay when falling)
y     = x * 10^(−gr/20) * makeup
low  *= 10^(boost/20)                       // per-band boost trim
```

OEM values are interpreted in 0.1 dB units for threshold/makeup/boost (e.g.
`-120` → −12.0 dB, `30` → 3.0 dB, `65` → 6.5 dB), which gives musically sane
settings on this hardware.

## 5. Loudness compensation

Optional, enabled only for modes where the OEM profile sets `LC Enable = 1`
(Movie, Game2). It is a smoothed low/high shelf pair:

* 110 Hz low shelf, up to +8 dB
* 6.5 kHz high shelf, up to +3.5 dB
* amount is `loud_amount × loud_volume`, smoothed over 150 ms
* coefficients are recomputed at control rate every 64 samples and filter state
  is preserved, so no clicks or zipper noise

This follows the equal-loudness principle (ISO 226) without claiming the OEM
curve.

## 6. Stereo width and crossfeed

Width is mid/side: `M = (L+R)/2`, `S = (L−R)/2`, `L = M + wS`, `R = M − wS`.
It is 1.0 (off) unless the OEM mode declares `Eagle-Wide` (Movie/Game2 → 1.12)
or `Eagle-Front` (Game1 → 1.05).

Headphone crossfeed is a Bauer/BS2B-style network: a 0.35 ms delayed,
700 Hz low-passed opposite channel is added, and the same-side low-passed signal
is subtracted so mono content is preserved. Default 0.25 for headphones, 0 for
speakers.

## 7. Protection: lookahead limiter and soft clip

A monotonic deque maintains the peak over the next `N` samples
(`N = lookahead_ms × fs`, 240 at 48 kHz). Gain is reduced before the peak
arrives:

```
target = min(1, ceiling / window_max)
gain  += (target < gain ? attack : release) * (target − gain)
out    = delayed_input * gain
out    = soft_limit(out, ceiling)
```

`soft_limit` is linear below 0.72 × ceiling, then a `tanh` knee to the ceiling.
Tested with +20 dB input: peak stays below the ceiling at 44.1/48/96 kHz.

## 8. Parameter smoothing and real-time discipline

* All gains/amounts/widths are one-pole smoothed (40–150 ms).
* Biquad coefficients are static except the loudness shelves, which update at
  1/64 sample rate.
* No malloc/free/lock/I/O in `dtsx_chain_process`.
* Filters keep state across control changes.
* Denormals flushed; FTZ/DAZ enabled on x86-64.

## 9. Profile parameter map

| Config key | Meaning | Source |
|---|---|---|
| `hpf_enable/hz/order` | speaker high-pass | OEM `EFX:Eagle-HPF *` |
| `eq_type/f/q/g` | 19-band fit | decoded GPEQ response |
| `bass_*` | harmonic bass | OEM TBHDX controls |
| `mbhl_*` | 3-band dynamics | OEM EFX MBHL controls |
| `loud_*` | loudness shelves | OEM LC enable + independent curve |
| `width` | M/S width | OEM `Eagle-Stereo Mode` |
| `crossfeed` | headphone crossfeed | independent, replaces undecoded I3DA |
| `ceiling_db` | limiter ceiling | safety choice (−1 / −1.5 dBFS) |
| `output_gain_db` | level trim | chosen for headroom |

## 10. What is intentionally not done

* No claim of HPEQ reproduction: the blob format is undecoded.
* No dynamic bass/EQ classifier: the OEM `VAEQ` algorithm is unknown.
* No virtual surround: the OEM `I3DA` is unknown and Music mode has it off.
* No bit-exact TruBass/MBHL emulation: only the control intent is matched.
* No automatic device switching: the user selects the virtual sink.

## 11. Safety rationale

The default profiles boost low frequencies less than the decoded OEM curve's
relative +10 dB around 200–315 Hz would suggest, because that boost is delivered
by harmonic synthesis and dynamics rather than raw sub-bass. The dry low band is
high-passed, harmonic content is band-limited and soft-saturated, and every
output passes a −1 dBFS lookahead limiter. Users who want more low end can raise
`bass_amount`, but the safe range is documented in the README.
