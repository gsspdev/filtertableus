# FilterTableUS — Interface contracts & threading rules

Frozen after Phase 0: `core/include/ftc/*`, `include/ftus/*`, `source/plugin/*`,
`cmake/*` + root/`core`/`core-tests` CMakeLists. Only the Wave-3 integration agent may amend
them, and must note the change here. If a frozen interface blocks you, implement what you can
against it and report the problem — do NOT edit it.

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

## Seams (who implements what the frozen shell calls)

- `ftus::createFtusEditor(FtusAudioProcessor&)` — `source/gui/` (stub: GenericAudioProcessorEditor)
- `ftus::createLoaderService(callback)` — `source/wavetable/` (stub: error-only). Callback fires
  on the message thread with `LoadResult{ok, errorMessage, table, info}`.
- `ftus::createStateManager(FtusAudioProcessor&)` — `source/state/` (stub: APVTS-only)
- `ftus::generateFactoryTable(FactoryTableId)` — `source/wavetable/FactoryGenerators.cpp`
  (declared only in Phase 0; the codec deliberately does NOT call it — StateManagerImpl
  regenerates factory tables itself: `generateFactoryTable` → `WavetableData::analyze` →
  `processor.adoptWavetable`)
- `ftc::FilterTableEngine` — `core/src/engine_stub.cpp` (passthrough) until Wave 2 replaces it
- `ftc::ModulationEngine` — `core/src/mod_stub.cpp` (zeros) until workstream D replaces it
- `ftc::KernelGenerator`, `ftc::PartitionedConvolver`/`KernelImage`/`ConvolutionSection` —
  declared, unimplemented until A2/A1 land (nothing in the scaffold references them; your .cpps
  bring the definitions)

## Scaling contracts (repeated from headers)

- Cutoff maps wavetable harmonic **24** to `cutoffHz`; harmonic k sits at `k*cutoffHz/24`.
- Mod depths: `toScan` ±1 = ±full scan range; `toCutoff` ±1 = ±48 semitones; `keytrack` ±1 =
  ±1 st per st from A4 (MIDI 69). Engine clamps scan to 0..1 and cutoff to
  `[20, min(22000, 0.45*fs)]` Hz AFTER summing.
- `RealFFT::forward` is unscaled; `inverse` scales 1/N; z-domain path is unscaled — fold 1/N
  into `zconvolveAccumulate`'s scale.
- `ResponseCurve::frequencyForPoint` is the ONLY frequency grid for the GUI spectrum.

## Known Phase 0 warts (for the integration agent)

- `JUCE_DISPLAY_SPLASH_SCREEN=0` is a no-op in JUCE 8.0.14 (flag removed upstream; warning at
  compile is expected from JUCE itself).
- The stub engine ignores Mix/outGain (pure passthrough); E's render assertions stay in their
  "stub tier" until the real engine lands (detect via `wet != dry` behavior, not via the static
  `latencySamplesFor`, which already returns the real design values).
- `resources/fonts` / `resources/presets/factory` binary-data targets GLOB `*.ttf/*.otf` and
  `*.ftpreset` respectively (+ placeholder.txt); owners drop files and reconfigure.
