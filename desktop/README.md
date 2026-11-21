# DTS Control Center

Native Tauri desktop control surface for the independent DTS:X-inspired Linux
DSP chain. It edits compatible user profiles and publishes complete live
snapshots to the running LADSPA instance over the existing POSIX shared-memory
transport.

## Development

```bash
cd desktop
npm --prefix frontend install
npm --prefix frontend run dev
cargo tauri dev
```

The application must be launched through Tauri: opening the Vite page in a
regular browser intentionally has no system-control backend.

## Release build

```bash
cd desktop
npm --prefix frontend run build
cargo tauri build --bundles rpm
```

The Fedora bundle requires the normal Tauri/WebKitGTK/RPM build dependencies.

## What live controls do

The desktop backend writes the active profile atomically, retains unknown JSON
keys, makes a `.json.bak` copy before replacement, then calls the C bridge.
The bridge uses `dtsx_config_load()` and `dtsx_live_params_from_config()` from
the DSP project before publishing the shared snapshot. The LADSPA plugin reads
one lock-free snapshot at an audio-block boundary and applies it without I/O,
locking, allocation, logging, or UI communication on the audio callback.

Live: bypass, graphic EQ, bass settings, loudness, MBHL enable/configuration,
width, crossfeed, input/output gain and ceiling. Limiter timing and lookahead
are reload-only because changing their allocated delay storage while running is
not safe.

## PipeWire status

Device discovery consumes structured `pw-dump` JSON. A sink is selectable only
after it is present in that graph. If DTS appears only as a filter and not as a
sink, Diagnostics reports that condition rather than claiming audio routing.

## Configuration locations

- Active profiles: `~/.config/dtsx/speakers.json` and `headphones.json`
- User profiles: `~/.config/dtsx/profiles/`
- Backups: adjacent `*.json.bak`

## Verification

```bash
make
make test
cd desktop
cargo check
npm --prefix frontend run build
```

`make test` includes a LADSPA integration test that confirms a published
snapshot changes the actual plugin output to bypass passthrough.
