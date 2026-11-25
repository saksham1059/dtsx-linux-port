# Architecture

```text
React/Vite UI → Tauri commands → profile manager / PipeWire inspection
                                      ↓
                              C profile bridge
                                      ↓
          POSIX shared-memory snapshot (seqlock, per speakers/headphones)
                                      ↓
          LADSPA plugin block-boundary read → C11 DSP chain → PipeWire
```

Rust does not duplicate DSP maths or configuration conversion. It validates
the editable UI range, atomically persists JSON, and delegates conversion to
`dtsx_config_load()` and `dtsx_live_params_from_config()` in C. C repeats its
own defensive parameter clamping before a chain consumes a snapshot.

The plugin opens its shared-memory page during instantiation. Its `run()`
method performs only a bounded seqlock read and in-place configuration update
before per-sample processing. The page is never opened, mapped, written, or
logged from the audio callback.
