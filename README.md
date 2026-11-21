# DTSX Linux Port — HP Victus DTS:X-inspired audio for Linux

This project reconstructs the **audible behaviour** of the Windows DTS:X Ultra
stack shipped with the HP Victus 15-fa2xxx (Realtek ALC245) as an independent
Linux implementation. It is not DTS code, does not link or run Windows binaries,
and does not claim bit-exact equivalence.

> This is a clean-room Linux reimplementation of the user-facing HP Victus
> DTS:X Ultra audio experience, not a port of Windows DTS binaries. See
> [PUBLIC_REPOSITORY.md](PUBLIC_REPOSITORY.md) for the public-source boundary.

What it does:

* applies the speaker-correction voicing recovered from the OEM GPEQ blobs
  (as a 19-band parametric fit, not as copied coefficients),
* adds a psychoacoustic harmonic bass stage following the OEM TruBass HDX
  control intent,
* applies the OEM's 100 Hz speaker high-pass and a three-band dynamics stage
  following the MBHL controls,
* provides optional loudness compensation, stereo width, headphone crossfeed,
  and a lookahead limiter,
* runs as a PipeWire virtual sink through a small LADSPA plugin.

Measured on this laptop: 54× realtime, 1.8 % of one core, 0.012 dB agreement
between the C filters and their design, 0.057 % THD at 1 kHz, output limited to
−1 dBFS.

---

## 1. Requirements

* Fedora (tested 44) with PipeWire 1.6 and `pipewire-pulse`
* `gcc`, `make`, `python3`, `numpy`, `scipy` (for regenerating profiles and
  measurements only; the runtime needs neither Python nor SciPy)
* No root. Everything installs under `~/.local` and `~/.config`.

The DSP core is C11 because this machine has no C++ compiler installed.

## 2. Build and test

```bash
cd ~/projects/dts
make            # build/dtsdsp and build/dtsx_ladspa.so
make test       # DSP tests, CLI self-test, LADSPA ABI test
```

Offline rendering:

```bash
./build/dtsdsp render -p configs/speakers-music.json -i in.wav -o out.wav
./build/dtsdsp info   -p configs/speakers-music.json
./build/dtsdsp bench  -p configs/speakers-music.json
```

`info` prints the static EQ response and the limiter latency; `bench` reports
CPU usage.

## 3. Install

```bash
./scripts/install.sh            # install files only
./scripts/install.sh --enable   # also create the two virtual sinks
```

`--enable` adds:

* `~/.config/pipewire/pipewire.conf.d/90-dtsx-speakers.conf`
* `~/.config/pipewire/pipewire.conf.d/91-dtsx-headphones.conf`
* `~/.config/systemd/user/pipewire.service.d/10-dtsx-ladspa.conf`
  (`LADSPA_PATH` for the plugin; PipeWire's LADSPA loader searches a path list)

Nothing existing is overwritten. Existing active profiles are kept unless you
pass `--force`.

Activate after installing:

```bash
systemctl --user restart pipewire pipewire-pulse wireplumber
```

or pass `--restart` to the installer.

## 4. Use

Two virtual output devices appear:

* **DTS:X-inspired Speakers**
* **DTS:X-inspired Headphones**

Select the matching one as your output device (GNOME Settings → Sound, or your
mixer). The filter chain processes the stream and forwards it to the hardware
sink.

Speaker modes:

```bash
./scripts/dtsx-mode.sh music     # default: GPEQ mode1, TBHDX 29, HPF 100
./scripts/dtsx-mode.sh movie     # GPEQ mode2, wide, loudness on
./scripts/dtsx-mode.sh voice     # GPEQ mode3, less bass
./scripts/dtsx-mode.sh game1     # GPEQ mode4, front
./scripts/dtsx-mode.sh game2     # GPEQ mode5, wide, loudness on
./scripts/dtsx-mode.sh headphones
```

Each copies the corresponding profile to `~/.config/dtsx/{speakers,headphones}.json`
and prints the restart command; the plugin reads the profile at instantiation.

To bypass the processing without changing devices, set `"bypass": 1` in the
active profile (or add a `Bypass` control in the filter-chain config) and
restart PipeWire.

## DTS Control Center desktop app

The `desktop/` directory contains the Tauri 2 + React control center. It
controls the existing C11/PipeWire DSP chain and does not implement a second
DSP engine. See [desktop/README.md](desktop/README.md) for development,
packaging and live-parameter behaviour.

## 5. Profiles and tuning

Profiles are flat JSON files in `configs/` (and installed in
`~/.config/dtsx/profiles/`). Important keys:

| Key | Range | Notes |
|---|---|---|
| `bass_amount` | 0…0.55 | harmonic bass strength; conservative cap |
| `bass_drive` | 1…2.5 | rectifier drive |
| `bass_extend_db` | 0…6 | low-shelf makeup |
| `bass_size_hz` | 40…80 | dry-bass protection high-pass |
| `mbhl_enable` | 0/1 | 3-band dynamics |
| `mbhl_boost_db` | 0…10 | low-band trim |
| `loud_enable` / `loud_amount` | 0/1, 0…1 | loudness compensation |
| `width` | 0.8…1.3 | M/S width |
| `crossfeed` | 0…0.5 | headphones only |
| `ceiling_db` | −6…−0.1 | limiter ceiling |
| `output_gain_db` | −12…+12 | level trim |

The EQ is a list of `eq_type` (0 peak, 1 low shelf, 2 high shelf, 3 HPF, 4 LPF,
5 notch, 6 allpass), `eq_f`, `eq_q`, `eq_g`.

Regenerate the profiles and the measurement set from the OEM evidence:

```bash
python3 python/build_configs.py --write
python3 python/measure.py
```

## 6. Safety

* Speaker profiles protect the driver with a 55–80 Hz high-pass, band-limited
  and saturated harmonics, and a −1 dBFS lookahead limiter.
* Do not raise `bass_amount` above 0.55 or disable the limiter for long-term
  listening on laptop speakers.
* The plugin never allocates or blocks in the audio callback.
* Install/uninstall only touch their own named files.

## 7. Uninstall

```bash
./scripts/uninstall.sh           # remove plugin, binary and drop-ins
./scripts/uninstall.sh --purge   # also remove ~/.config/dtsx
systemctl --user restart pipewire pipewire-pulse wireplumber
```

Your original device and routing are untouched; select the hardware sink again.

## 8. Troubleshooting

**No “DTS:X-inspired” device appears**
Check the drop-ins exist and restart all three services. Inspect
`journalctl --user -u pipewire -b` for LADSPA errors.

**`failed to load plugin 'dtsx_ladspa'`**
The LADSPA loader joins `LADSPA_PATH` entries with the plugin name + `.so`.
Confirm `~/.config/systemd/user/pipewire.service.d/10-dtsx-ladspa.conf` exists
and contains the directory that holds `dtsx_ladspa.so`, then restart PipeWire.

**Audio is silent or plays on the wrong device**
The filter chain's playback stream is passive; make sure the hardware sink is
available and not suspended. Select the virtual sink again after a device
change.

**Crackles / xruns**
The chain uses ~1.8 % of one core at 48 kHz. Raise PipeWire's quantum if the
system is loaded; the limiter adds 5 ms latency but no CPU spikes.

**Too bright / too much bass**
Edit the active profile: reduce the high-shelf bands (`eq_g` for the last
bands), lower `bass_amount`, or reduce `mbhl_boost_db`. Regenerate from
`python/build_configs.py` only if you want to discard manual edits.

## 9. Honest limitations

* **No Windows reference capture exists**, so "sounds like Windows" is a
  design goal, not a measured result.
* **HPEQ is not decoded.** The headphone profile follows the OEM control intent
  and adds a conservative crossfeed; it is not the OEM headphone correction.
* **TruBass, MBHL and loudness are independent approximations** of the OEM
  control values; their DSP internals were not decompiled.
* **I3DA / VAEQ / PEREQ are not implemented.** Music mode has I3DA off anyway.
* The 5 ms limiter latency is not reported to PipeWire.
* No automatic speaker/headphone switching.

## 10. Project map

| Path | Content |
|---|---|
| `PROJECT_HANDOVER.md` | full project map, confirmed/disproven/unknown |
| `PROJECT_STATUS.md` | live milestone status and next tasks |
| `docs/ARCHITECTURE.md` | runtime architecture and integration |
| `docs/REVERSE_ENGINEERING_FINDINGS.md` | instruction-level evidence |
| `docs/DSP_DESIGN.md` | algorithms and parameter mapping |
| `docs/VALIDATION.md` | objective results and method |
| `research/RESEARCH_LOG.md` | public sources and equations |
| `src/`, `include/`, `tests/` | implementation and tests |
| `python/`, `measurements/` | analysis, profile generation, results |
| `scripts/` | install, uninstall, mode switch, measurement |

License: GPL-2.0-or-later for the original code in this project. OEM binaries,
blobs and the licence file in `evidence/` remain the property of their owners
and are included only as analysis evidence; they are not part of the built
product.
