# FilterTableUS — Calibration TODO (by-ear A/B vs. the original Kilohearts Filter Table)

Wave-3 verified every knob below is *functional and test-locked*; what no offline render can
decide is whether the **voicing matches the original product**. A human with the original
plugin (or its demo renders) should A/B each item on the same wavetable + input and adjust.
Every knob is a one-site change; after each change run
`scripts/run_all_checks.sh build/w3` (a few tests assert current defaults and say so in
their failure text).

How to A/B: load the same wavetable in both plugins, match Scan/Cutoff/Resonance/Mix, feed a
saw loop (or the test suite's 110 Hz saw), and toggle one knob at a time. Rebuild with e.g.
`cmake -B build/cal -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-DFTUS_SCAN_LERP_DB=1"`.

| # | Knob (plan §9) | Default (as shipped) | Where | What to listen for |
|---|---|---|---|---|
| 1 | **Resonance curve + reference** — highest risk | `k_r = 4^r` dB-contrast about the in-band mean of harmonics 1..1024 (floored dB, DC excluded) | `core/src/kernelgenerator.cpp`, `applyResonance()` (single site: mean definition, k_r curve, floor) | Does +res sharpen peaks/deepen troughs at the same *rate* as the original? Does −res flatten identically? If the original's res reads as "local emphasis" rather than "global contrast", switch the reference to a cepstrally-smoothed local envelope (variant pre-designed in plan §2.3) |
| 2 | **ORIGINAL/RAW construction** — high risk | Original: band-limit k·fc/24 > fs/2, 2048-pt irfft, circular Catmull-Rom to period P0 = 24·fs/fc anchored at tap L/2, Tukey, response-peak normalize. Raw: causal from tap 0, no band-limit/window, Σh² = 1 | `core/src/kernelgenerator.cpp`, `cyclicPath()` | Character of Original at low cutoff (cycle repetition audible?), Raw's aliasing/gain behavior vs. the original's Raw |
| 3 | **Scan-morph domain** | Linear-magnitude lerp (spectral path) / complex lerp (cyclic path) | `FTUS_SCAN_LERP_DB` (0→1 for dB-domain morph), one lerp site in `kernelgenerator.cpp` | Mid-scan brightness between two very different frames: linear-mag favors the louder frame; dB-domain sweeps "darker" through the middle |
| 4 | **Below-harmonic-1 policy** | `LowEndPolicy::InterpToDC` (interpolate toward the frame's DC bin ≈ natural LF rolloff) | `FTUS_LOW_END_POLICY` (0→1 for HoldH1) in `kernelgenerator.cpp` | Bass content with cutoff high: does the original keep energy below fc/24 (HoldH1) or roll it off (InterpToDC)? |
| 5 | **RAW normalization** | Σh² = 1 (energy), per contract; peaks may exceed 0 dB (audition measured ×1.7 on a saw) | `core/src/kernelgenerator.cpp`, `cyclicPath()` Raw branch | Raw-mode loudness match vs. the original when toggling modes on the same table |
| 6 | **LINEAR window α (notch depth vs. pre-echo)** | Tukey(0.25) (taper L/8 per side), symmetric about the compiled center | `kernelgenerator.cpp`, window construction in `prepare()` | Notch depth on comb/notch tables in Linear mode; pre-echo audibility on transients |
| 7 | **LINEAR symmetry center** | `FTUS_LINEAR_HALF_SAMPLE_CENTER=0` (integer L/2 → exact 50 %-mix null, decided in Wave 3) | `kernelgenerator.cpp` | Only revisit if the original demonstrably does NOT null at 50 % mix (ours nulls to −155 dBFS; variant 1 leaves a half-sample HF comb) |
| 8 | **Update cadence / crossfade length (zipper vs. smear)** | Kernel tick 128/256/512 samples by fs tier (~2.67 ms), crossfade = one tick; smoothing 15 ms (scan/res/log2-cutoff), 10 ms mix/gain ramps | `core/include/ftc/EngineConfig.h` (one file) | Fast Scan sweeps: audible stepping (→ shorten tick / lengthen smoothing) vs. smeared response (→ opposite). Engine zipper test bounds regressions |
| 9 | **Loudness normalization across scan** | Response peak → 0 dB every kernel (resonance reads as "everything else ducks") | `kernelgenerator.cpp`, normalize step in `spectralPath()`/`cyclicPath()` | Perceived loudness ride while scanning bright↔dark frames vs. the original (white-noise input, LUFS meter) |
| 10 | **Kernel-gen cost @192 kHz** (containment, not voicing) | Inline generation; Minimum @192 k measured ~2 ms ≈ 75–85 % of its 512-sample tick on a loaded machine | `EngineConfig::kernelUpdateInterval` (halve cadence at 192 k) or build the pre-designed background worker (plan §2) | Not audible A/B — profile on the target machine; act only if 192 k + small buffers glitches |

Also worth an ear (not §9 but adjacent):
- **Preset gain staging**: audition renders (see `tests/integration/test_preset_audition.cpp`
  output) show "Synced Sweep Bass" and "Random Steps" sitting ~25 dB below the loudest preset
  on the same saw — intentional (dark bass filters), but check against taste and bump their
  `outGain` in `resources/presets/factory/*.ftpreset` if desired.
- **Mode-switch outage** (~10 ms + new-mode latency of wet silence): compare against the
  original's mode-switch behavior; ours favors correctness (no phase-misaligned overlap).
