# Development notes

Keep the C layout in `include/dtsx_live.h` authoritative. The desktop backend
does not mirror that layout; the C bridge owns conversion of disk JSON into a
`DtsxLiveParams` snapshot. This avoids an ABI drift between Rust and the
plugin.

All input identifiers are allowlisted to letters, digits, `.`, `_` and `-`.
PipeWire commands use fixed argument vectors and device IDs discovered from
the current graph; no UI text is evaluated as a shell command.
