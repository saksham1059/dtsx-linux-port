# Validation

Two different things must not be confused:

1. **Internal validation** — does the C implementation realise the design
   (filters, limiter, dynamics, profiles) accurately and safely? This is what the
   automated suite measures.
2. **Comparative validation against Windows** — does it sound like DTS:X Ultra
   on the same hardware? **No Windows reference capture exists in this
   workspace.** We therefore make no numerical claim of equivalence. Subjective
   listening and any future Windows-vs-Linux acoustic comparison are separate
   work.

## Reproduce

```bash
make test
python3 python/measure.py
```

Outputs: `measurements/validation.md`, `validation-*.csv`, `validation-*.svg`,
and `validation-spectrum.*`.

## Results (this machine, 2026-11-14)

### Linear response

The validation profile disables the nonlinear bass stage, dynamics, loudness and
width, leaving HPF + 19-band EQ + limiter. Measured with sine tones at
−20 dBFS and compared to the analytic design:

| Rate | RMS error | Max error |
|---|---|---|
| 48,000 Hz | 0.012 dB | 0.064 dB |
| 44,100 Hz | 0.012 dB | 0.063 dB |

This proves the C biquads, HPF order and gain staging match the Python model
that was fitted to the decoded OEM response. It does **not** prove the fit is
the OEM response — that is separately bounded by the fit residual (0.44–0.83 dB
RMS in band).

### EQ fit residual (design stage)

| Mode | In-band RMS | In-band max |
|---|---|---|
| Music | 0.44 dB | 3.52 dB |
| Movie | 0.83 dB | 5.49 dB |
| Voice | 0.67 dB | 5.77 dB |
| Game1/2 | 0.72 dB | 5.98 dB |

### Latency

Impulse peak offset: **240 samples = 5.00 ms** at 48 kHz (220 at 44.1 kHz).
This is the limiter lookahead. LADSPA does not report it to the host, so video
sync can be off by this amount.

### Distortion

THD (harmonics 2–10) at 1 kHz, −6 dBFS through the full speaker chain:
**0.057 % (−64.9 dB)**.

### Limiter

A −6 dBFS 200 Hz tone with +12 dB output gain, ceiling −1.0 dBFS:

* output peak **0.8369**, i.e. **−1.55 dBFS**
* no samples above the ceiling
* +20 dB test in the self-test also stays bounded at 44.1/48/96 kHz

### Pink noise

5 s pink noise at −18 dBFS through the speaker profile:

* output peak 0.2365 (−12.52 dBFS)
* output RMS 0.0494 (−26.12 dBFS)

The third-octave spectrum is in `measurements/validation-spectrum.svg`.

### CPU

```
processed 10.0 s of stereo audio in 0.183 s (≈54x realtime, 1.8 % of one core)
```

### PipeWire

An isolated PipeWire instance (temporary `XDG_RUNTIME_DIR`, the generated
drop-in, `LADSPA_PATH` pointing at the build directory) starts without errors
and creates:

```
effect_input.dtsx_speakers
effect_output.dtsx_speakers
```

The headphone descriptor is exercised by the LADSPA ABI test. Install and
uninstall were tested in a sandbox `HOME`; they add and remove only their own
files.

## Stability tests

`tests/test_dsp.c` and `build/dtsdsp selftest` cover:

* peaking response accuracy (±0.35 dB at the centre)
* HPF attenuation
* 5 s resonant biquad stability
* LR4 magnitude reconstruction (ratio 0.9968)
* limiter ceiling and gain reduction
* full-chain stability at 44.1, 48 and 96 kHz
* silence preservation, bit-transparent bypass
* config round-trip
* LADSPA instantiate/run/cleanup for both labels

All pass.

## What remains unvalidated

* Acoustic output at the listener (no microphone measurement was made).
* Windows comparison (no capture).
* HPEQ, TruBass and MBHL exactness (undecoded internals).
* Long-term thermal/behavioural effects of high bass settings.
* Inter-sample (true-peak) behaviour beyond the soft clip.

## Interpreting the numbers honestly

A 0.012 dB match between the C code and the design means the code is faithful to
its own model. The model's accuracy against the OEM is bounded by the GPEQ fit
residual and by the fact that the OEM's TruBass/MBHL/loudness stages are
approximated. The correct claim is:

> an independent, evidence-driven reconstruction that reproduces the OEM
> speaker-correction curve to well under 1 dB RMS in the audible band and
> implements the OEM control intent for bass and dynamics.
