# Agent B — Wavetable I/O (import, converter, factory tables, loader service)

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan section: §4 (authoritative for this brief).

## Owns (exclusive)
- `source/wavetable/**` (including its `CMakeLists.txt`; replace the Phase 0 `LoaderServiceStub.cpp` — you may delete it)
- `tests/unit/wavetable/**`
- Build dir: `build/b` (full plugin build — you need JUCE audio formats; run unit tests via ctest filtered to your suite)

## What to build
1. **`WavImporter`** — the decision ladder from plan §4 exactly: valid WAV check → (stretch, only if time permits: Serum `clm ` chunk frame-size hint) → length divisible by 2048 (≤4096 frames) → wavetable with even-stride decimation to ≤256 frames keeping endpoints → short file 256–8192 samples → single-cycle resampled to one 2048 frame → else hand to the converter. Multi-channel → mono mixdown 0.5·(L+R). Every frame: DC-remove, peak-normalize to 0.9, wrap-crossfade the last 16 samples into the first 16.
2. **`SampleConverter`** — YIN pitch detection (frame 4096, hop 1024, range 30–2000 Hz, threshold 0.15, parabolic interpolation, median filter width 5), global median f0; slice consecutive periods starting at the strongest rising zero-crossing; circular Catmull-Rom resample each period to 2048; ≤256 periods → all, else 256 evenly strided; same per-frame normalization; voiced-frame ratio < 40% or no stable median → error "Couldn't detect a pitch — try a 2048-multiple wavetable or a short single-cycle file".
3. **`FactoryGenerators`** — implement `generateFactoryTable(FactoryTableId)` for all 12 IDs in `include/ftus/FactoryTables.h`. Pure, deterministic (fixed seeds), built from integer-multiple harmonic partials so frames loop perfectly; 16–256 frames each; the plan names them (AnalogMorph sine→tri→saw→square 64f, Pwm width 50→3% 128f, VowelMorph A-E-I-O-U formant-filterbank over saw 128f, CombSweep, NotchArray, HarmonicLadder 1→32 partials, OddEvenMorph, SpectralDrift seeded smoothed random spectra, FormantPeaks 2-resonance sweep, DigitalSteps quantized saw morph, MetalCluster high-harmonic clusters, SubBloom low-pass rolloff opening). Musical quality matters — make each table actually morph interestingly across frames.
4. **`LoaderServiceImpl`** — implements the frozen `LoaderService` interface: one background `juce::Thread` + mutex-guarded job deque (enqueue from message thread only); worker: import/convert → `ftc::WavetableData::analyze` → deliver `LoadResult` on the message thread (`MessageManager::callAsync`); atomic float progress; errors as clean `LoadResult{ok=false, message}`; never crashes on malformed input. `requestFactoryTable` generates (memoized message-thread cache is fine) then analyzes off-thread.

## Acceptance (Catch2 in tests/unit/wavetable, headless-safe)
1. Import round-trip: synthesize a 4×2048 WAV to a temp dir → import → frames bit-equal (post-normalization steps applied consistently — test the pipeline's determinism, not raw equality: import the same file twice → identical output).
2. 300×2048 file → 256 frames, first/last input frames preserved as endpoints.
3. Single-cycle path: 600-sample saw file → 1 frame, 2048 samples, periodic (wrap discontinuity < 0.05).
4. Garbage: truncated WAV, text file renamed .wav, 0-byte file → clean error, no crash, no partial table.
5. Converter: synthetic 110/220/440 Hz saw + −30 dB noise → detected f0 within 0.5%; output numFrames ≥ 1; frames normalized; unpitched input (white noise) → the clean pitch error.
6. All 12 factory IDs: numFrames ∈ [16, 256], no NaN/inf, peak ≤ 1.0, wrap continuity |x[2047·]−x[0·]| < 0.05 per frame; deterministic across two calls.
7. LoaderServiceImpl: load completes on a background thread with result delivered (use a message-loop pump in the test), progress reaches 1.0, error path delivers ok=false.
