# Research log

Public sources used to design the implementation. This log is limited to public
literature and documentation; no OEM binaries, profile blobs, or private
reverse-engineering artifacts are included in this repository. Equations are
quoted only to the extent needed to justify the code.

---

### R1. Biquad coefficient design (RBJ audio-EQ cookbook)

* Source: <https://webaudio.github.io/Audio-EQ-Cookbook/Audio-EQ-Cookbook.txt>
* Finding: closed-form coefficients for peaking, shelving, HP/LP, notch and
  allpass filters using `w0`, `alpha = sin(w0)/(2Q)` and `A = 10^(dB/40)`.
* Key equations used in `src/core/biquad.c`:

```
peaking:  b0 = 1 + alpha*A     b1 = -2cos(w0)   b2 = 1 - alpha*A
          a0 = 1 + alpha/A     a1 = -2cos(w0)   a2 = 1 - alpha/A
low shelf: b0 = A((A+1) - (A-1)cos + 2sqrt(A)alpha), ...
```

* Impact: all static EQ and crossover sections. Coefficients are normalised by
  `a0` and processed in TDF-II.

### R2. Transposed direct form II and numerical behaviour

* Source: A. Oppenheim, R. Schafer, *Discrete-Time Signal Processing*;
  J. O. Smith, *Introduction to Digital Filters*,
  <https://ccrma.stanford.edu/~jos/filters/>
* Finding: TDF-II needs two state words per section, has lower coefficient
  sensitivity than direct form I for narrow bands, and is well suited to
  floating point.
* Impact: `DtsxBiquad` layout (`z1,z2`) and the state stride in the reference
  decoder match this form exactly.

### R3. Linkwitz-Riley crossovers

* Source: <https://en.wikipedia.org/wiki/Linkwitz%E2%80%93Riley_filter>
* Finding: an LR4 is two cascaded 2nd-order Butterworth sections; low+high sum
  to an all-pass with flat magnitude.
* Equations: `Q = 1/sqrt(2)` per section; `LP4 = LP2·LP2`, `HP4 = HP2·HP2`.
* Impact: `src/filters/crossover.c`; measured reconstruction ratio 0.9968.

### R4. Lookahead limiter and compressor design

* Source: D. Giannoulis, M. Massberg, J. Reiss, "Digital Dynamic Range
  Compressor Design — A Tutorial and Analysis", JAES 60(6), 2012,
  <https://www.eecs.qmul.ac.uk/~josh/documents/2012/GiannoulisMassbergReiss-dynamicrangecompression-JAES2012.pdf>
* Finding: feed-forward gain computers, separate attack/release smoothing in
  dB, and the ratio/knee/threshold formulation used here.
* Impact: `src/dynamics/mbhl.c` gain computer; `src/dynamics/limiter.c` uses a
  sliding maximum over a lookahead window so gain reduction precedes peaks.

### R5. Missing fundamental / virtual pitch

* Source: E. Terhardt, "Calculating virtual pitch", *Hearing Research* 1(2),
  1979, <https://doi.org/10.1016/0378-5955(79)90025-X>
  (record: <https://artikelvinder.kb.nl/journal/0000000010782/volumes/1/issues/2/articles/1427310666496>)
* Finding: the auditory system infers a fundamental from resolved harmonics even
  when the fundamental is weak or absent.
* Impact: justifies harmonic bass synthesis on small drivers and the decision to
  attenuate the dry sub-bass while emphasising harmonics. The implementation is
  an engineering approximation, not a model of Terhardt's algorithm.

### R6. Psychoacoustics of loudness

* Source: E. Zwicker, H. Fastl, *Psychoacoustics: Facts and Models*, Springer;
  ISO 226:2003 equal-loudness contours,
  <https://www.iso.org/standard/34222.html>
* Finding: equal-loudness contours flatten at high SPL; at lower volumes the ear
  is less sensitive to low and (to a lesser degree) high frequencies.
* Impact: `src/loudness/loudness.c` uses low/high shelves whose gain grows as the
  control value moves toward "quiet". The OEM's own LC curve is not decoded, so
  the module is optional and conservative.

### R7. Program loudness measurement

* Source: ITU-R BS.1770-4, <https://www.itu.int/rec/R-REC-BS.1770>
* Finding: K-weighted, gated loudness measurement.
* Impact: not implemented; the validation suite uses RMS and relative spectra.
  A future version could use BS.1770 for level matching against Windows.

### R8. Headphone crossfeed

* Source: B. Bauer, "Stereophonic Earphones and Binaural Loudspeakers", JAES
  1961; BS2B project, <https://bs2b.sourceforge.net/>
* Finding: a delayed, low-passed opposite-channel signal reduces the unnatural
  lateralisation of hard-panned headphone content while preserving mono.
* Impact: `src/spatial/spatial.c` (0.35 ms delay, 700 Hz LP, same-side
  subtraction). The OEM I3DA is not decoded; crossfeed is an independent,
  conservative substitute.

### R9. PipeWire filter-chain

* Source: <https://docs.pipewire.org/page_module_filter_chain.html>
* Finding: a filter-chain module creates a capture stream and a playback stream;
  `capture.props.media.class = Audio/Sink` makes it a virtual sink, and
  `playback.props.node.passive = true` lets the session manager link it to the
  hardware sink. LADSPA plugin lookup joins each `LADSPA_PATH` entry with
  `<plugin>.so`.
* Impact: `configs/pipewire/*.conf.in`, `scripts/install.sh` (sets
  `LADSPA_PATH` through a systemd user drop-in), and the verified isolated
  instance test.

### R10. Windows audio processing objects

* Source: Microsoft, "Audio Processing Object Architecture",
  <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/audio-processing-object-architecture>
* Finding: APOs run in the audio engine and are registered per endpoint; SFX/MFX/
  EFX are stream, mode and endpoint effects.
* Impact: explains the OEM `chain_v2` SFX → OSFX → EFX order and why the
  processing is software, not a hidden hardware EQ.

### R11. HP Victus platform identification

* Source: HP Victus 15-fa2xxx Maintenance and Service Guide,
  <https://kaas.hpcloud.hp.com/pdf-public/pdf_11551870_en-US-1.pdf>
* Finding: dual speakers, DTS:X Ultra, HP Audio Boost; confirms the product
  family and that the feature is a software stack.
* Impact: target identification only; no coefficients taken from it.

### R12. Real-time audio constraints

* Source: PipeWire documentation, <https://docs.pipewire.org/>; R. Bencina,
  "Real-time audio programming 101" (public article)
* Finding: no allocation, locking or blocking I/O in the process callback;
  preallocate, use lock-free communication for control changes.
* Impact: the engine allocates only in `dtsx_chain_create`; parameters are
  smoothed, not applied atomically with locks.

---

## Summary of the public design basis

| Implementation decision | Public basis |
|---|---|
| Biquad forms and EQ | R1, R2 |
| LR4 crossover | R3 |
| Dynamics topology | R4 |
| Harmonic bass | R5, R6 |
| Loudness shelves | R6 |
| Crossfeed | R8 |
| PipeWire integration | R9 |
| Audio processing objects | R10 |
