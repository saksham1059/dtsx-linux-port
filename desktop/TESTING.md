# Testing

Run the core suite from the repository root:

```bash
make
make test
```

The suite checks biquad response, numerical stability, crossover
reconstruction, limiter ceiling, offline chain behaviour, CLI self-test and
the LADSPA ABI. The LADSPA test additionally opens the exact shared-memory
page used by the plugin, publishes a bypass snapshot, runs a further block and
requires sample-exact passthrough. This is an integration assertion that the
running plugin applies a live parameter, not merely that a page can be opened.

Desktop checks require installed JavaScript and Rust/Tauri dependencies:

```bash
cd desktop
cargo check
npm --prefix frontend run build
```

Manual PipeWire check: start the app, use Devices to select a discovered DTS
sink, then check Diagnostics. A virtual sink is only shown as detected when it
appears as `Audio/Sink` in `pw-dump`.
