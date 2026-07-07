# FilterTableUS — Interface contracts & threading rules

Frozen after Phase 0: `core/include/ftc/*`, `include/ftus/*`, `source/plugin/*`,
`cmake/*` + root/`core`/`core-tests` CMakeLists. Only the Wave-3 integration agent may amend
them, and must note the change here. If a frozen interface blocks you, implement what you can
against it and report the problem — do NOT edit it.

## Wave-3 changes to frozen surfaces (2026-07-07, integration agent)

1. `source/plugin/PluginProcessor.{h,cpp}` — `prepareToPlay` now fills `ftc::Parameters` from
   the cached atomics and calls `engine.setParameters(...)` BEFORE `engine.prepare(...)`, so a
   session restored to Linear/Original reports its L/2 latency immediately (the engine seeds
   its active mode from the last parameter snapshot). Safe: `setParameters` is a lock-free POD
   copy and audio is stopped during `prepareToPlay`.
2. `source/plugin/PluginProcessor.{h,cpp}` — the parameter-bypass early return is gone. Bypass
   (parameter or `processBlockBypassed`, now overridden) processes normally with `mix` forced
   to 0 and `outGainDb` forced to 0: bypassed audio runs through the engine's dry delay of
   exactly the REPORTED latency (time-aligned in Linear/Original), the 10 ms ramps make the
   toggle click-free, and the wet state stays warm so un-bypassing is seamless. The same
   forcing is applied to the pre-`prepare` snapshot so a bypassed session restore starts
   silent-wet. Locked by `tests/integration/test_wave3_integration.cpp`.
3. `source/plugin/Parameters.cpp` — every float parameter now has explicit
   `stringFromValue`/`valueFromString` attributes (fixed-decimal, unit-labelled: Hz/dB/ms/%,
   percent scaling for 0..1 and −1..+1 params). Fixed decimals make text→value→text a fixed
   point, which CLAP's `param-conversions` validator requires; parsers tolerate unit suffixes.
   Display strings in hosts change (e.g. "0.42" → "42.0" with label "%"); parameter IDs,
   ranges, defaults and the GUI's own readout formatting are untouched.
4. `core/src/kernelgenerator.cpp` (A2's file, coordinated change) —
   `FTUS_LINEAR_HALF_SAMPLE_CENTER` default flipped 1 → 0: the LINEAR kernel is built
   symmetric about integer tap L/2 (group delay exactly the reported L/2), so LINEAR at
   mix 50% nulls broadband against the latency-aligned dry path (measured −155 dBFS residual;
   the engine suite auto-tightened to the strict −60 dBFS assertion). The variant-0 Tukey
   window + symmetrization pair about L/2 (tap 0 zeroed) so the symmetry is exact by
   construction. The half-sample variant (=1) remains selectable for calibration A/B.
   `core/tests/test_kernel_spectral.cpp` symmetry test detects the compiled center.
5. `source/state/StateManagerImpl.cpp` + `source/gui/PluginEditor.cpp` (comments) — GUI scale
   has ONE serialized home: the session `<GUI scale="..."/>` node. The runtime home stays the
   `"guiScale"` root property on the live APVTS tree (editor reads at open/writes on resize);
   it is stripped from the serialized `<PARAMS>` on save and from loaded blobs on apply
   (pre-Wave-3 blobs carried a duplicate). This made save→load→save byte-idempotent, fixing
   clap-validator `state-reproducibility-basic`/`-null-cookies`/`state-buffered-streams`.
   Presets never carry scale.
6. `scripts/run_all_checks.sh` — clap-validator promoted advisory → REQUIRED.
   `scripts/validate_clap.sh` — clears the validator's per-plugin temp dir before runs
   (clap-validator 0.3.2 panics on leftovers from previously FAILING runs).
7. No changes were needed in `core/include/ftc/*`, `include/ftus/*`, `cmake/*`, or any
   CMakeLists — the frozen headers held as designed.

## Ownership map (create/modify/delete ONLY inside your paths)

| Workstream | Owns |
|---|---|
| A1 convolution | `core/src/convolver*`, `core/tests/test_convolver*` |
| A2 kernel generation | `core/src/kernel*`, `core/src/wavetabledata.cpp`, `core/tests/test_kernel*`, `core/tests/test_wavetabledata.cpp` |
| B wavetable I/O | `source/wavetable/**`, `tests/unit/wavetable/**` |
| C GUI | `source/gui/**`, `resources/fonts/**` |
| D modulation + state | `core/src/mod*`, `core/tests/test_mod*`, `source/state/**`, `resources/presets/**`, `tests/unit/state/**` |
| Engine assembly (Wave 2) | `core/src/engine*` (deletes `engine_stub.cpp`), `core/tests/test_engine*` |
| E harness | `tests/integration/**`, `scripts/**`, `.github/**` |

Source lists are GLOBbed (`CONFIGURE_DEPENDS`) everywhere except `source/plugin` — add files in
your directories and reconfigure; never edit CMakeLists outside your ownership. Every agent
builds in its own `build/<agent>` directory. Core-only iteration:
`cmake -B build/<agent> -G Ninja -DFTUS_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release`.

## Thread model

| Thread | Runs | May touch |
|---|---|---|
| Message | GUI, attachments, preset/state IO, load-result adoption, latency AsyncUpdater, 1 Hz `engine.collectGarbage()` | APVTS, engine non-RT API (`setWavetable`, `collectGarbage`, `currentTableForUi`), LoaderService requests, StateManager |
| Loader (inside LoaderServiceImpl) | file decode, sample conversion, `WavetableData::analyze` | its own buffers; delivers via `MessageManager::callAsync` |
| Audio | `processBlock`: param atomics → `ftc::Parameters` → `engine.setParameters` + `engine.process` | preallocated engine state, relaxed atomics. NEVER: locks, allocation, logging, exceptions, shared_ptr release |

## Hard rules

1. **Audio thread**: no `new/malloc/free`, no locks, no `std::function` invocation that may
   allocate, no exceptions, no unbounded loops. Everything sized worst-case in `prepare()`.
   `ftc::ScopedNoDenormals` (or JUCE's, shell-side) active in every process call.
2. **Wavetable lifetime**: only `ftc::ObjectHandoff` moves tables to the audio thread; the
   audio thread never destroys anything (message-thread GC keeps pending/current/previous).
3. **GUI**: controls bind ONLY via Slider/ComboBox/Button/ParameterAttachment (waterfall uses a
   raw `juce::ParameterAttachment` with begin/end gestures). Visual data flows audio→UI only
   via `TripleBuffer` (`engine.readResponseCurve`) and relaxed atomics
   (`engine.currentScan/envValue`), polled by Timers (~30 Hz). The editor never calls engine
   processing paths, never blocks, never touches DSP state directly.
4. **Latency**: only the engine knows it (`latencySamples()`, changes only on phase-mode
   switch). The frozen processor renotifies the host via AsyncUpdater. Report exactly 0
   (Minimum/Raw) or L/2 (Linear/Original); it must NOT vary with cutoff/scan.
5. **State**: `getState/setState` may run off the message thread — no GUI work there without a
   `MessageManager` guard. Adopt decoded tables via `FtusAudioProcessor::adoptWavetable`.
6. **No agent runs git commands.** The orchestrator commits at wave boundaries.
7. **Parameters**: IDs in `ftus/PluginIDs.h` are permanent (27 of them, version hint 1). Choice
   orders must match the `ftc` enums (`PhaseMode`, `LfoShape`, `SyncDivision`) — locked by test.

## Seams (all live as of Wave 3 — no stubs remain)

- `ftus::createFtusEditor(FtusAudioProcessor&)` — `source/gui/PluginEditor.cpp` (full editor)
- `ftus::createLoaderService(callback)` — `source/wavetable/LoaderServiceImpl.cpp`. Callback
  fires on the message thread with `LoadResult{ok, errorMessage, table, info}`.
- `ftus::createStateManager(FtusAudioProcessor&)` — `source/state/StateManagerImpl.cpp`
- `ftus::generateFactoryTable(FactoryTableId)` — `source/wavetable/FactoryGenerators.cpp`
  (the codec deliberately does NOT call it — StateManagerImpl regenerates factory tables
  itself: `generateFactoryTable` → `WavetableData::analyze` → `processor.adoptWavetable`)
- `ftc::FilterTableEngine` — `core/src/engine.cpp` (real engine, Wave 2)
- `ftc::ModulationEngine` — `core/src/mod.cpp` (workstream D)
- `ftc::KernelGenerator` — `core/src/kernelgenerator.cpp` (A2);
  `ftc::PartitionedConvolver`/`KernelImage`/`ConvolutionSection` — `core/src/convolver.cpp` (A1)

## Scaling contracts (repeated from headers)

- Cutoff maps wavetable harmonic **24** to `cutoffHz`; harmonic k sits at `k*cutoffHz/24`.
- Mod depths: `toScan` ±1 = ±full scan range; `toCutoff` ±1 = ±48 semitones; `keytrack` ±1 =
  ±1 st per st from A4 (MIDI 69). Engine clamps scan to 0..1 and cutoff to
  `[20, min(22000, 0.45*fs)]` Hz AFTER summing.
- `RealFFT::forward` is unscaled; `inverse` scales 1/N; z-domain path is unscaled — fold 1/N
  into `zconvolveAccumulate`'s scale.
- `ResponseCurve::frequencyForPoint` is the ONLY frequency grid for the GUI spectrum.

## Known warts (as built, after Wave 3)

- `JUCE_DISPLAY_SPLASH_SCREEN=0` is a no-op in JUCE 8.0.14 (flag removed upstream; warning at
  compile is expected from JUCE itself).
- `resources/fonts` / `resources/presets/factory` binary-data targets GLOB `*.ttf/*.otf` and
  `*.ftpreset` respectively (+ placeholder.txt); owners drop files and reconfigure.
- **Mode-switch transition** is not just the two 5 ms fades: 5 ms wet fade-out → hard kernel
  swap + dry-retap → fade-in that first HOLDS at zero for the new mode's latency (the reset
  convolver outputs silence until its pipeline fills), then ramps 5 ms. Total wet outage
  ≈ 10 ms + new-mode latency (e.g. ≈ 31 ms entering Linear/Original @48 k). The dry share of
  the mix keeps flowing throughout; reported latency flips at the REQUEST tick.
- **ResponseCurve** is the FFT of the truly-built kernel (one preallocated 2L-point real FFT
  at kernel builds), sampled on the frozen 256-point grid — it includes windowing/truncation
  effects, so it is the honest response, not the idealized design magnitude.
- `FilterTableEngine::currentTableForUi()` returns the message-thread mirror `shared_ptr`
  without synchronization against a concurrent host `setState` thread. Hosts serialize state
  calls in practice; flagged for a future RT-safety review (D's report), not observed failing.
- `ModulationEngine.prepare` has no maxBlockSize; env tick-snapshots cover blocks ≤ 65536
  samples (larger blocks are chopped by the engine's defensive slicing anyway).
- Bypass while a mode switch is mid-fade transiently mixes the old-latency dry tap (same as
  normal operation during a switch); alignment is exact once the switch settles.
- clap-validator 0.3.2 reuses fixed temp paths and panics if a previously FAILING run left
  files behind; `scripts/validate_clap.sh` clears the plugin's dir first.
- The standalone smoke screenshot needs macOS screen-recording permission for the calling
  terminal; without it the step degrades to a launch check (advisory).
