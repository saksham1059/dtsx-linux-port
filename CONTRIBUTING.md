# Contributing

Contributions should preserve the project’s clean-room boundary:

- Do not add proprietary DTS/HP binaries, decrypted blobs, private dumps, or
  material copied from non-public software.
- Keep real-time code allocation-free, lock-free, and free of filesystem,
  logging, or UI work in the processing callback.
- Add or update tests for DSP, LADSPA, profile, or desktop-control changes.
- Preserve existing historical analysis and document evidence levels for new
  conclusions.

Before opening a change, run:

```bash
make
make test
python3 python/measure.py
```

The OEM inputs used for profile generation are intentionally not distributed;
do not add proprietary binaries, private dumps, or extracted OEM data to a
change. For desktop changes, also follow
[desktop/TESTING.md](desktop/TESTING.md).
