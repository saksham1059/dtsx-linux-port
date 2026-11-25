# dtsx: an independent DTS:X-inspired audio chain for Linux

This project provides a PipeWire/LADSPA audio-processing chain tuned for the
internal speakers of the HP Victus 15-fa2xxx with Realtek ALC245. It is an
independent implementation, not DTS software, and does not claim bit-exact or
acoustic equivalence to Windows DTS:X Ultra.

The chain includes a speaker EQ, harmonic-bass enhancement, high-pass
protection, three-band dynamics, optional loudness and stereo processing, and
a lookahead limiter. It also includes headphone settings, a command-line
renderer, and a Tauri control-center application.

## Why this project exists

On this HP Victus, the built-in speakers sound better in Windows than in Linux.
That difference is not necessarily a hardware problem: Windows includes the
laptop maker's DTS:X Ultra processing and model-specific tuning, while a
standard Linux audio setup usually sends sound through a more generic path
without that speaker correction and enhancement. This project exists to give
Linux a practical, laptop-specific alternative using its native PipeWire audio
stack, rather than relying on Windows drivers or binaries.

## Build and test

Requirements: a C11 compiler, `make`, and `libm`. The runtime has no Python
dependency. Python 3 and NumPy are needed only to regenerate validation
measurements.

```sh
make
make test
```

Render a WAV file or inspect a profile:

```sh
./build/dtsdsp render -p configs/speakers-music.json -i input.wav -o output.wav
./build/dtsdsp info -p configs/speakers-music.json
```

## Install

```sh
./scripts/install.sh
./scripts/install.sh --enable
systemctl --user restart pipewire pipewire-pulse wireplumber
```

Choose **DTS:X-inspired Speakers** or **DTS:X-inspired Headphones** as the
output device. Change speaker profiles with
`./scripts/dtsx-mode.sh music|movie|voice|game1|game2`; uninstall with
`./scripts/uninstall.sh`. Installation is per-user and does not overwrite
existing audio configuration.

## Desktop application

See [desktop/README.md](desktop/README.md) for development and packaging.
Building the Tauri app requires its Linux GTK/WebKitGTK development
dependencies in addition to Rust and Node.js.

## Profiles, validation, and limitations

The checked-in profiles are ready to use. OEM binaries, raw configuration
blobs, licence material, and private reverse-engineering artifacts are
intentionally not included. As a result, profile regeneration from those
inputs is not part of this public package.

Run the C tests with `make test`. To regenerate the DSP validation results,
install NumPy (`python3 -m pip install -r requirements.txt`), build the
project, and run:

```sh
python3 python/measure.py
```

The measurements check the implementation against its own DSP model; no
Windows reference audio capture is available, so they do not establish
equivalence to Windows. HPEQ is not decoded, and the bass, dynamics, and
loudness stages are independent approximations. The limiter adds 5 ms latency,
which is not reported to PipeWire.

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md),
[docs/DSP_DESIGN.md](docs/DSP_DESIGN.md), and
[docs/VALIDATION.md](docs/VALIDATION.md) for implementation details.

## License and names

Original project code is licensed under GPL-2.0-or-later; see [LICENSE](LICENSE).
DTS and DTS:X are trademarks of their respective owner. This project is
independent and is not affiliated with or endorsed by DTS or HP.
