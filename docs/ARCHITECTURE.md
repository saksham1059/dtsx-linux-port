# Architecture

## 1. What runs where

```
application audio
      │  (PipeWire / PulseAudio client)
      ▼
" effect_input.dtsx_speakers "  ← virtual sink created by module-filter-chain
      │
      ▼
LADSPA plugin  dtsx_ladspa.so   label dtsx_speakers
      │  loads ~/.config/dtsx/speakers.json at instantiate
      ▼
DtsxChain (C11, real-time, allocation-free)
      │
      ▼
" effect_output.dtsx_speakers " → default hardware sink (passive stream)
```

The headphone path is identical with label `dtsx_headphones` and
`~/.config/dtsx/headphones.json`.

The offline CLI (`build/dtsdsp`) uses the same `DtsxChain` so measurements and
listening use identical code.

## 2. Signal chain (evidence-based order)

```
in gain (TruBass input gain, −3 dB typical)
  ├─ TruBass HDX-inspired harmonic bass     [SFX stage in the OEM chain]
  ├─ 19-band speaker correction EQ          [SFX AEQ / decoded GPEQ curve]
  ├─ high-pass 100 Hz, 2nd order            [EFX]
  ├─ 3-band dynamics (MBHL-inspired)        [EFX]
  ├─ loudness compensation (optional)       [LC, Movie/Game2 only]
  ├─ stereo width (0 unless OEM mode is Wide/Front)
  ├─ output gain
  └─ lookahead limiter + soft clip          [protection]
```

The implementation uses a documented signal-chain order and independently
designed processing stages; it does not include or load Windows code or data.

## 3. Module map

| Path | Responsibility |
|---|---|
| `src/core/biquad.c` | RBJ biquad design, TDF-II processing, denormal flush |
| `src/core/chain.c` | chain assembly, ordering, gains, response prediction |
| `src/core/config.c` | flat-JSON profile reader/writer |
| `src/filters/crossover.c` | LR4 split/recombine, soft clip |
| `src/bass/truhdx.c` | harmonic bass synthesis |
| `src/dynamics/mbhl.c` | 3-band compressor with per-band makeup/boost |
| `src/dynamics/limiter.c` | sliding-max lookahead limiter |
| `src/loudness/loudness.c` | control-rate low/high shelf compensation |
| `src/spatial/spatial.c` | M/S width + delayed low-pass crossfeed |
| `src/io/wav.c` | WAV read/write (16/24/32-bit PCM, float32) |
| `src/cli/dtsdsp.c` | render/stream/info/bench/selftest |
| `src/ladspa/dtsx_ladspa.c` | LADSPA descriptors and RT glue |
| `python/gpeq.py` | Frequency-response helpers used by validation |
| `python/measure.py` | objective validation and plots |

## 4. Real-time properties

* Everything is allocated in `dtsx_chain_create` / LADSPA `instantiate`.
* `dtsx_chain_process` performs no allocation, locking, I/O or logging.
* Coefficient changes are only made by the loudness module at control rate
  (every 64 samples), preserving filter state; no per-sample `cos`/`pow`.
* Denormals are flushed in biquad state and FTZ/DAZ is enabled on x86-64.
* Fixed block budget: the chain is O(samples × bands) with constant memory.

Measured: 54.7× realtime for stereo 48 kHz on this laptop (1.83 % of one core).

## 5. Latency

The limiter uses a 5 ms lookahead (`floor(fs × lookahead_ms/1000)` samples:
240 at 48 kHz, 220 at 44.1 kHz). The LADSPA plugin does not expose a latency
port, so PipeWire does not compensate it. For video, a 5 ms offset is below one
video frame but is documented as a limitation.

## 6. Profiles

`configs/speakers-{music,movie,voice,game1,game2}.json` are ready-to-use
profiles with independent 19-band speaker EQ settings.

`configs/headphones-music.json` mirrors the OEM headphone control intent
(no HPF, small TruBass, MBHL boost 30, crossfeed instead of I3DA) but does not
claim the HPEQ correction, which is undecoded.

The active profiles are copied to `~/.config/dtsx/{speakers,headphones}.json`
by `scripts/install.sh` and changed with `scripts/dtsx-mode.sh`.

## 7. Why LADSPA and not LV2 / a native SPA filter

* PipeWire's `module-filter-chain` loads LADSPA, LV2 and builtin filters; LADSPA
  needs no additional dependency or SDK headers.
* A native SPA filter would give latency reporting and lower overhead but
  requires PipeWire development headers and ABI-specific code; not justified
  for a first working system.
* EasyEffects cannot load arbitrary custom DSP without an LV2 plugin; the
  virtual-sink design is independent of the session's effect stack and easy to
  roll back.

## 8. Safety decisions

* Default ceiling −1 dBFS (speakers), −1.5 dBFS (headphones).
* The low band is high-passed at 55–80 Hz and harmonics are soft-saturated, so
  small drivers are not driven with unbounded sub-bass.
* Bass amount defaults are conservative (0.09–0.35, never > 0.55).
* The chain never exceeds the ceiling in tests, including +20 dB input.
* Install never edits existing PipeWire configuration; it only adds uniquely
  named drop-ins and a systemd user environment drop-in, both removed by
  `scripts/uninstall.sh`.
