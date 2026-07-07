# FilterTableUS — Clone of Kilohearts "Filter Table" (JUCE 8 / C++20)

> Durable copy of the approved plan (original session plan file: `~/.claude/plans/make-a-plan-to-crispy-hopcroft.md`).
> Execution state, resume instructions, and agent briefs live in `docs/EXECUTION.md` and `docs/briefs/`.

## Amendments since approval (2026-07-07)

1. **Dependencies are pre-cloned locally** under `external/` (gitignored) and pinned in `external/PINS.txt`: **JUCE 8.0.14** (supersedes the 8.0.8 pin below), clap-juce-extensions @ 16e9d4c (submodules initialized), Catch2 v3.7.1, pffft @ a4b0359. `cmake/Dependencies.cmake` must prefer these local checkouts (`add_subdirectory` if present) with pinned FetchContent as fallback — one shared checkout for all parallel build dirs, zero network at configure time.
2. **Network policy for agents**: no `git clone`/`ls-remote`; the only downloads are the AGPL license text and the pluginval binary, via `curl --max-time 120 --retry 2` with non-blocking fallbacks (two scaffold runs stalled on hung network calls).
3. **Execution is orchestrated via workflows** with independent verification gates (scaffold → verify → fix loop), per the ultracode directive.

---

## Context

Build a from-scratch clone of the Kilohearts **Filter Table** audio effect (https://kilohearts.com/products/filter_table) in the empty repo `/Users/music/Developer/filtertableus`. The plan is structured for **execution by several coding agents in parallel**: one serial scaffold phase freezes all shared interfaces and ships compiling stubs (the plugin builds and passes pluginval from day 1), then six workstreams with strict file ownership run concurrently, then serial integration.

**What Filter Table is** (researched: product page, kilohearts.com/docs, launch blog, KVR):
- Wavetable frames are converted into **filter frequency responses**; scanning the table smoothly morphs the filter. Frames are **2048 samples**; Kilohearts-native tables are exactly **256 frames** (WAV or FLAC); Serum-style imports are any N×2048 (import accepts 1–256).
- Controls: **Scan** (position in table), **Cutoff** (the frequency where wavetable *harmonic 24* lands → harmonic k sits at k·cutoff/24; keytrackable), **Resonance** (emphasizes or attenuates response peaks and troughs), **Mix** (dry/wet; intentionally not phase-compensated except in linear mode), and four **phase modes**: **Minimum** (min-phase kernel, zero latency), **Linear** (linear-phase, latency, aligned with dry), **Original** (preserves the frame's own phases, latency), **Raw** (frame waveform used directly as convolution kernel, artifacts allowed).
- Import: drag-and-drop wavetables + sample→wavetable conversion (pitch detection, period slicing). UI: 3D waterfall of frames (draggable), spectrum plot of current response, knob row, phase-mode selector, presets. Ships VST3/AU/AAX.

## Decisions (confirmed with user + design synthesis)

| Decision | Choice |
|---|---|
| Framework | **JUCE 8 (local checkout 8.0.14) / C++20 / CMake**; Catch2 v3.7.1 for tests |
| Scope | **Core plugin + internal modulation** (2 LFOs, envelope follower, MIDI keytrack → Scan/Cutoff). No in-plugin wavetable editor. |
| Formats | **VST3 + AU + Standalone + CLAP** (clap-juce-extensions, pinned). AAX excluded. |
| DSP core | **JUCE-free static lib `ftcore`** with vendored **pffft** (BSD-like, `pffft_zconvolve_accumulate` is purpose-built for our inner loop) → unit tests build/run in seconds without JUCE |
| Kernel generation | **Inline on the audio thread in v1** (pure, preallocated, ~25–40 µs @48k vs 2.67 ms budget); background-worker escape hatch pre-designed (SPSC request ring + triple-buffered kernel slots) — build only if profiling shows spikes >30% of a control period (likely only 192 kHz + tiny buffers) |
| UI | Same functional layout as the original, **own visual skin** (dark theme, "signal orange" `#FF8A3D` accent, Inter font — deliberately unlike Kilohearts) |
| Platform | macOS first (arm64+x86_64 universal Release; native-arch Debug); Windows-compatible code, no Windows CI yet. Dev box: macOS 13, Xcode 15.2, CMake 3.31. |
| Legal | Original code/assets only; product name **FilterTableUS**; no Kilohearts branding or artwork; factory wavetables generated algorithmically. License **AGPLv3** (JUCE free tier, splash off). If it ever goes closed-source: JUCE license or splash on. |

## 1. Repo layout & build

```
filtertableus/
├── CMakeLists.txt                  # project(), OSX archs BEFORE project(), options
├── cmake/{Dependencies,Warnings,PluginSetup}.cmake   # local-checkout-first deps; ftus::warnings; juce_add_plugin
├── external/                       # ★ gitignored local dep checkouts: JUCE, clap-juce-extensions, Catch2, pffft (PINS.txt)
├── core/                           # ★ JUCE-FREE ftcore static lib (namespace ftc)
│   ├── include/ftc/                #   frozen headers: Types.h EngineConfig FFT.h FastMath.h
│   │                               #   Denormals.h AlignedVector.h RealtimeExchange.h Parameters.h
│   │                               #   WavetableData.h Kernel.h KernelGenerator.h Convolver.h
│   │                               #   Modulation.h ResponseCurve.h FilterTableEngine.h
│   ├── src/                        #   [A1: convolver*] [A2: kernel*, wavetabledata] [D: mod*] [wave-2: engine*]
│   ├── third_party/pffft/          #   vendored (copied from external/pffft, license kept)
│   └── tests/                      #   Catch2, JUCE-free; helpers/ (analytic frames, naive conv, click detector)
├── include/ftus/                   # ★ frozen shell headers: PluginIDs.h LoaderService.h
│                                   #   StateManager.h WavetableCodec.h FactoryTables.h
├── source/
│   ├── plugin/                     # [Phase 0, then frozen until integration] PluginProcessor,
│   │                               #   Parameters.cpp (APVTS from PluginIDs), WavetableCodecImpl (gzip+base64)
│   ├── wavetable/                  # [B] WavImporter, SampleConverter, FactoryGenerators, LoaderServiceImpl
│   ├── gui/                        # [C] PluginEditor, Theme/FtusLookAndFeel, WaterfallView, SpectrumView,
│   │                               #   KnobPanel, PhaseModeSelector, ModPanel, PresetBar, WavetableRow, ValueReadout
│   └── state/                      # [D] StateManagerImpl (presets)
├── resources/fonts/  resources/presets/factory/      # [C] Inter (SIL OFL) · [D] *.ftpreset XML
├── tests/{unit/*,integration/}     # unit dirs owned per agent; integration/ [E]
├── scripts/                        # [E] build.sh validate_vst3.sh validate_au.sh validate_clap.sh smoke_standalone.sh
├── .github/workflows/ci.yml        # [E, stretch]
└── docs/{ARCHITECTURE.md,INTERFACES.md,PLAN.md,EXECUTION.md,briefs/}
```

**Parallel-ownership trick**: the root CMake never lists sources — each `source/<area>/CMakeLists.txt` calls `target_sources(FilterTableUS PRIVATE …)` and is owned by exactly one agent; core sources are grouped per-agent by filename prefix. No two agents ever edit the same file. **Each agent uses its own build dir (`build/<agent>`)** — concurrent builds in one dir race.

**Plugin target** (`cmake/PluginSetup.cmake`): `juce_add_plugin(FilterTableUS … COMPANY_NAME "FilterTableUS Project" BUNDLE_ID com.filtertableus.FilterTableUS PLUGIN_MANUFACTURER_CODE FtUs PLUGIN_CODE Ftbl FORMATS VST3 AU Standalone IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE AU_MAIN_TYPE kAudioUnitType_MusicEffect VST3_CATEGORIES Fx Filter COPY_PLUGIN_AFTER_BUILD TRUE MICROPHONE_PERMISSION_ENABLED TRUE)` + `clap_juce_extensions_plugin(TARGET FilterTableUS CLAP_ID "com.filtertableus.filtertable" CLAP_FEATURES audio-effect filter stereo)`. **AU must be `aumf` (music effect) or hosts won't deliver MIDI to an FX.** Defines: `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0`. Modules: audio_utils, dsp, audio_formats, gui_extra. Buses {1→1, 2→2}. `CMAKE_OSX_DEPLOYMENT_TARGET 10.13`; ban availability-gated std lib features (`std::format`, `std::atomic<shared_ptr>`, `jthread`) — core ships its own primitives. Compiler: no `-ffast-math` (breaks NaN guards); `-fno-math-errno -ffp-contract=fast` OK. Warnings-as-errors on our targets only. Generator: Ninja (or fallback), universal Release / native-arch Debug.

## 2. DSP core design (`ftcore`)

**Flow**: host block → chopped at **control ticks (64 samples)** → modulators + smoothers produce effective {scan, cutoff, resonance} → change detector (|Δscan|>2⁻¹², |Δlog2 fc|>1 cent, |Δres|>2⁻¹⁰) → at most every **kernel tick (~2.67 ms: 128/256/512 samples @≤50k/≤100k/≤200k)** `KernelGenerator::generate()` builds a new FIR kernel inline → `ConvolutionSection::pushKernel` crossfades to it. Static params → generator idle.

**Constants** (`EngineConfig`, centralized): kernel L = 2048/4096/8192 by fs tier (constant ~43 ms support, ~23 Hz resolution); design FFT N = 4L; direct head 128 taps; partition 128 (FFT 256); control tick 64; crossfade = one kernel tick; smoothing ~15 ms one-pole for scan/res and cutoff-in-log2, ~10 ms per-sample linear ramps for mix/output gain; magnitude floor −120 dB.

**Analysis** (`WavetableData::analyze`, loader thread, allocates — never audio thread): per frame one 2048-pt real FFT → stores per frame the 1025-bin **magnitude array** (spectral path) and **complex spectrum** (cyclic path) + raw frames; immutable, `shared_ptr<const>`, ~5 MB max. Published to audio via `ObjectHandoff` (raw-pointer acquire on audio thread; message-thread graveyard GC frees retired tables — audio thread never runs a destructor).

**Response construction** (per kernel update):
1. **Scan**: framePos = scan·(F−1); spectral path (MIN/LINEAR) lerps the two adjacent frames' **linear magnitudes**; cyclic path (ORIGINAL/RAW) lerps **complex spectra** (≡ the time-domain wavetable crossfade every wavetable synth does). dB-domain morph kept as a one-line compile-time switch *(calibrate)*.
2. **Cutoff mapping, per output bin** (above-Nyquist handling automatic — we read, never place): bin k at f_k has continuous harmonic index h = 24·f_k/fc. 1≤h≤1024: lerp adjacent harmonic magnitudes. h<1: interpolate toward the frame's DC value (≈0 → natural LF rolloff; `LowEndPolicy{InterpToDC|HoldH1}` compile-time *(calibrate)*). h>1024: zero with a short taper — at low cutoff the filter genuinely closes above 1024·fc/24 (expected product behavior). fc_eff = param × keytrack × mod, clamped [20 Hz, min(22 kHz, 0.45·fs)], smoothed in log2.
3. **Resonance** r ∈ [−1,+1]: dB-contrast about the in-band mean — k_r = 4^r; `L'db = Lref + k_r·(Ldb − Lref)`; floor at (max−120 dB). v2 variant behind same signature: cepstrally-smoothed local envelope as Lref *(calibrate — highest-risk item)*.
4. **Normalize peak magnitude to 0 dB** (filter only attenuates; resonance reads as "everything else ducks"; no clipping by construction). `outGain` provides makeup.

**Kernel reconstruction (4 modes)**:
- **MINIMUM** (latency 0): real-cepstrum min phase — ln-mag → irfft_N → causal fold (c[0], 2c[1..N/2−1], c[N/2], 0…) → rfft_N → complex exp → irfft_N → truncate L, half-Hann fade last L/8. N=4L bounds cepstral aliasing.
- **LINEAR** (L/2): zero-phase irfft_N → rotate L/2 → truncate → symmetric Tukey(0.25) window. Exact tap symmetry.
- **ORIGINAL** (L/2, constant — PDC must not chase the cutoff knob): resonance on |S| with phases kept → band-limit (zero harmonics mapping above Nyquist) → irfft_2048 = one cycle → circular Catmull-Rom resample so one cycle spans P₀ = 24·fs/fc samples (wrap to fill L if P₀>L) → center at L/2, Tukey window.
- **RAW** (latency 0): same cyclic path but no band-limit (aliasing is the mode's contract), no window, causal from tap 0, hard truncate, energy (Σh²=1) normalization.

**Convolution** (`PartitionedConvolver` mono + `ConvolutionSection` stereo): **zero-latency two-stage** — 128-tap direct SIMD head + uniform 128-sample FFT-partitioned tail (frequency-domain delay line, `pffft_zconvolve_accumulate`); tail partition j needs only completed past blocks → no added latency (Gardner scheduling). Any host block size incl. 1. Cost ≈ **<1% core stereo @48k** even while crossfading; generation dominates (~1.5% @48k, ~4–6% @192k inline).
**Time variation**: output crossfade of two convolver instances **is** exact kernel-space lerp (linearity) — `pushKernel` analyzes into the spare `KernelImage` (partition FFTs), memcpys FDL+head state from the active instance, then per-sample **linear** crossfade over one kernel tick; old instance idles after (steady-state 1×). `pushKernel` during a fade → retry next tick.

**Latency & mix**: MIN/RAW report 0; LINEAR/ORIGINAL report L/2 (21.3 ms at every fs since L scales). Internal **dry path is delayed by the reported latency** → LINEAR mix is phase-aligned (documented original behavior; null-testable), MIN/RAW mid-mix phasing is inherent and intended. **Mode switch** (latency change → cannot crossfade): 5 ms wet fade-out → hard kernel swap + convolver reset + dry-delay change → 5 ms fade-in; shell notifies `setLatencySamples` via message-thread AsyncUpdater. `processBlockBypassed` runs through the same dry delay.

**Modulation** (in core; evaluated at control ticks): **LFO ×2** — shapes sine/tri/saw↑/saw↓/square/S&H; free 0.02–20 Hz or tempo-synced 8/1…1/32 incl. dotted/triplet, **timeline-locked** phase from ppq while playing; optional note retrigger; seeded S&H. **EnvelopeFollower** — rectified mono input tap, one-pole attack 0.1–500 ms / release 1–2000 ms, sensitivity ±24 dB → 0..1. **NoteTracker** — last-note priority, hold on all-off. **Summing**: scanEff = clamp01(scan + Σ depth·src); cutoff in semitones — LFO/ENV depth ±48 st at full, keytrack = ±1 st per st from **A4 (MIDI 69)** so +100% = exact 1:1 tracking (cutoff default 440 Hz makes this musical). Net mod bandwidth bounded by the ~375 Hz kernel cadence — it's a scanned filter, not a phaser (matches original).

**RT safety**: everything preallocated in `prepare()` (worst case: max block, L for fs, 256 frames); no locks/allocation/exceptions on the audio thread; own scoped FTZ/DAZ guard (MXCSR / FPCR.FZ); per-block wet finiteness check → on NaN/Inf: reset convolver, silence one block, resume; silence-idle path skips work after L+tick of silence. Offline renders are deterministic by construction (inline generation).

**Facade** (`ftc::FilterTableEngine`): `prepare(spec)` · `reset()` · `setParameters(const Parameters&) noexcept` (POD copy, audio thread at block start — shell fills it from APVTS atomics) · `setWavetable(shared_ptr<const WavetableData>)` (message thread) · `process(float* const*, nCh, n, TransportInfo, span<NoteEvent>) noexcept` · `latencySamples()` + static `latencySamplesFor(mode, fs)` · `collectGarbage()` (message-thread 1 Hz) · UI taps: `TripleBuffer<ResponseCurve>` (256-pt log-grid dB curve, grid function in `ResponseCurve.h` so GUI can't drift) + atomics `currentScan`, `envValue`.

## 3. Plugin shell: parameters, state, presets

**Parameters** (all IDs/ranges/defaults/choice tables in `include/ftus/PluginIDs.h`; every param `ParameterID{id, versionHint=1}`; IDs permanent — range change = new ID + migration shim; golden-list test locks the set). 27 params:

| ID(s) | Type/Range | Default | Notes |
|---|---|---|---|
| `scan` | float 0–1 | 0 | shown as frame n/N |
| `cutoff` | float 20–20000 Hz, log skew | 440 Hz | harmonic-24 frequency |
| `resonance` | float **−1…+1** | 0 | bipolar ("emphasizes or attenuates") |
| `mix` | float 0–1 | 1.0 | |
| `phaseMode` | choice Minimum/Linear/Original/Raw | Minimum | zero-latency default |
| `keytrack` | float −1…+1 | 0 | ±1 st per st from A4 |
| `outGain` | float −24…+12 dB | 0 | |
| `bypass` | bool | off | `getBypassParameter()` |
| `lfo1Rate/Sync/Div/Shape/Retrig/ToScan/ToCutoff` | 0.02–20 Hz log / bool / 16 divisions / 6 shapes / bool / ±1 / ±1 | 1 Hz, off, 1/4, Sine, off, 0, 0 | ToScan ±1 = full range; ToCutoff ±1 = ±48 st |
| `lfo2…` (same 7) | same | rate 0.25 Hz | |
| `envSens/Attack/Release/ToScan/ToCutoff` | ±24 dB / 0.1–500 ms log / 1–2000 ms log / ±1 / ±1 | 0, 10 ms, 200 ms, 0, 0 | |

Audio thread reads all params via cached `getRawParameterValue()` atomics once per block → fills `ftc::Parameters`. Only `phaseMode` has a listener (→ AsyncUpdater → `setLatencySamples` on message thread).

**State** (`StateManagerImpl`): XML `<FilterTableUS stateVersion="1">` = APVTS state + `<WAVETABLE type="user|factory" factoryId name frames encoding="gzip-f32le-v1">base64</WAVETABLE>` + GUI scale + preset name/dirty. **Codec = raw f32-LE frames → juce GZIP → Base64** (lossless; FLAC rejected — integer quantization). Factory tables store only `factoryId` (regenerated deterministically). `setStateInformation` may run off the message thread: parse+replaceState+decode on calling thread, adopt via graveyard registry + `engine.setWavetable`, GUI refresh via guarded `callAsync`. **Presets**: same schema, `.ftpreset`, `~/Library/Application Support/FilterTableUS/Presets/`; 8–10 factory presets embedded via `juce_add_binary_data`; browser = flat list, prev/next wrap, save dialog, dirty `*`.

## 4. Wavetable I/O (`source/wavetable/`)

**Import ladder** (in order): (1) not a readable WAV → error. (2) `clm ` Serum chunk → honor frame size, resample to 2048 if needed *(stretch, v1.1)*. (3) length divisible by 2048 (≤4096 frames) → wavetable; >256 frames → even-stride decimation keeping endpoints. (4) short file 256–8192 samples → single-cycle, resample to one frame. (5) else → sample converter. Multi-channel → mono mixdown. All frames DC-removed, peak-normalized 0.9, last 16 samples wrap-crossfaded into first (kills Raw-mode clicks).

**Sample→wavetable converter**: **YIN** pitch detection (frame 4096, hop 1024, 30–2000 Hz, threshold 0.15, parabolic interp, median 5 — beats autocorrelation on octave errors), global median f0 (per-frame tracking = stretch); slice periods at strongest rising zero-crossings (phase-coherent waterfall); circular Catmull-Rom resample to 2048; ≤256 periods → all, else 256 evenly strided; voiced <40% → clean error, keep current table.

**Factory tables**: `generateFactoryTable(FactoryTableId)` — pure, deterministic, harmonic-partial-built (perfect loops), memoized on first use; 12 tables: Analog Morph, PWM, Vowel Morph A-E-I-O-U (formant filterbank over saw), Comb Sweep, Notch Array, Harmonic Ladder, Odd/Even Morph, Spectral Drift, Formant Peaks, Digital Steps, Metal Cluster, Sub Bloom.

**LoaderServiceImpl**: one background `juce::Thread` + mutex'd job deque (message-thread enqueue only); import/convert → `WavetableData::analyze` → `callAsync` adopt (graveyard + setWavetable + ChangeBroadcaster); atomic progress polled by GUI; errors → 4 s toast; never crashes on malformed files. Editor is a whole-surface `FileDragAndDropTarget` (.wav).

## 5. GUI (`source/gui/`)

**Editor 920×620**, resizable 0.75–1.75× fixed aspect, scale persisted. **Software rendering only** (no OpenGL — deprecated on macOS; worst case ~6k line segments @30 fps is trivial). Component tree: `PresetBar(0,0,920,36)` · `WavetableRow(0,36,920,34)` (name, frames, Load…, drop hint, progress/toast) · `WaterfallView(12,78,588,332)` · right column `SpectrumView(612,78,296,170)` + `PhaseModeSelector(612,256,296,40)` + small knobs Keytrack/Output · `KnobPanel(12,418,588,120)` Scan/Cutoff/Resonance/Mix (88 px rotaries) · `ModPanel(0,546,920,74)` tabs LFO1|LFO2|ENV (rate⇄div swap on sync, shape, retrig, 2 bipolar depth knobs; ENV adds live meter) · shared `ValueReadout` strip.

**WaterfallView**: pseudo-3D stacked polylines — ≤48 evenly-strided frames back-to-front, 128 pts each, offset dx+2.4/dy−4.6 px, alpha 1.0→0.18; active frame (from engine's post-mod `currentScan` atomic) drawn last in accent w/ under-glow. Click/drag = scan via `ParameterAttachment` gestures (shift=fine, double-click=0). Paths cached per table/resize; VBlank-gated ~30 fps repaint only when scan moved >1/512 or table changed.

**SpectrumView**: log-f 20 Hz–20 kHz, −30…+30 dB grid; reads engine's `TripleBuffer<ResponseCurve>` on a 30 Hz timer; filled accent curve. Grid = the mapping in `ftc/ResponseCurve.h` (producer/consumer can't drift).

**Rules** (docs/INTERFACES.md): controls bind only via Slider/ComboBox/Button/ParameterAttachment; visual data flows audio→UI via TripleBuffer/relaxed atomics polled by timers; editor never calls the engine; no locks/alloc on audio thread; file/preset/latency ops message-thread only. `FtusLookAndFeel`: bg `#141619`, panels `#1C1F26`, text `#C9CED6`, accent `#FF8A3D`; 270° arc rotaries, center-notched bipolar variant for depth/keytrack knobs; Inter font embedded.

## 6. Threading model

| Thread | Does |
|---|---|
| Message | GUI, attachments, preset/state IO, latency AsyncUpdater, graveyard GC (1 Hz), factory generation, load adoption |
| Loader (1 bg thread) | WAV decode, converter, `analyze()`; hands off via `callAsync` |
| Audio | params atomics → `ftc::Parameters` → `engine.setParameters` + `engine.process` (mod ticks, inline kernel gen, convolution, dry delay, mix). Never: locks, malloc, shared_ptr release |

Handoffs (all Phase 0 headers): APVTS raw atomics · `ObjectHandoff<WavetableData>` (publish shared_ptr, audio acquires raw ptr, message-thread GC frees when ≠ pending/current/previous — covers in-flight crossfades) · `TripleBuffer<T>` for response curve · relaxed atomics for scan/env meter.

## 7. Parallel-agent execution plan

**Ground rules**: strict file ownership (briefs in `docs/briefs/`); `core/include/ftc/*`, `include/ftus/*`, `source/plugin/*` are **frozen after Phase 0** (only the integration agent may amend, with a note in docs/INTERFACES.md); build stays green at every landing; **each agent uses its own build dir** `build/<agent>`; core agents build only `ftcore` + tests (`-DFTUS_BUILD_PLUGIN=OFF`) — no JUCE compile in their loop.

### Phase 0 — Scaffold (serial; full brief: `docs/briefs/brief-phase0.md`)
Build system + local-checkout deps + core foundation (FFT/FastMath/AlignedVector/Denormals/TripleBuffer/ObjectHandoff/EngineConfig, tested) + ALL frozen headers + shell wiring + working WavetableCodec + stubs (passthrough engine, zero mod, error loader, APVTS-only state, GenericAudioProcessorEditor) + test helpers. **Acceptance**: universal Release builds VST3+AU+CLAP+Standalone; ctest green; pluginval level 8 + auval (`aumf Ftbl FtUs`) pass on the stub.

### Wave 1 — six parallel agents (briefs: `docs/briefs/brief-{a1,a2,b,c,d,e}-*.md`)
| Agent | Owns | Delivers |
|---|---|---|
| A1 | `core/src/convolver*` | zero-latency two-stage partitioned convolver + A/B crossfade section |
| A2 | `core/src/kernel*`, `wavetabledata*` | analysis + response construction + 4 phase-mode kernel reconstruction |
| B | `source/wavetable/**` | import ladder, YIN converter, 12 factory tables, background loader |
| C | `source/gui/**` | full editor: waterfall, spectrum, panels, LNF, drag-drop |
| D | `core/src/mod*`, `source/state/**` | modulation engine; state embed/restore, presets |
| E | `tests/integration/**`, `scripts/**` | golden param test, offline render suite (auto-tightening), validation scripts, CI draft |

### Wave 2 — engine assembly (starts when A1+A2 land; brief: `docs/briefs/brief-engine.md`)
Real `FilterTableEngine`: block chopping, mod integration, smoothers + change detector, inline generation + crossfades, dry delay + mix, mode-switch fade-through-silence + latency, robustness, response-curve publication. Engine-level tests (latency, nulls, zipper, chunking invariance, determinism).

### Wave 3 — integration & polish (serial, final)
Land/verify order E → A1/A2 → engine → B → D → C (full build + ctest + pluginval after each); unfreeze `source/plugin` for final seams; DAW manual pass; auval both arches; clap-validator; pluginval L10 attempt; standalone screenshot; factory-preset audition; by-ear calibration vs. original demos (risk-register knobs); tag v0.1.0.

## 8. Verification

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build --output-on-failure
scripts/validate_vst3.sh    # pluginval --strictness-level 8 (10 = stretch)
scripts/validate_au.sh      # auval -strict -v aumf Ftbl FtUs
scripts/validate_clap.sh    # clap-validator if present
scripts/smoke_standalone.sh # launch, screenshot, quit
```
Manual end-to-end: load factory table → sweep Scan on a saw loop (smooth morph, no zipper) → all four phase modes (latency handled, LINEAR nulls vs dry at mix 50%) → drop a Serum wavetable → convert a vocal sample → keytrack from MIDI → save/reload session.

## 9. Risk register — calibrate by ear, don't chase precision

| Risk | Default | Containment |
|---|---|---|
| Resonance math (reference: global mean vs local envelope; range) — **highest** | k_r=4^r about in-band mean | one function; envelope variant pre-designed |
| ORIGINAL/RAW construction (cycle repetition, windowing, centering) — **high** | §2 cyclic path | isolated in one generator function |
| Inter-harmonic interpolation + below-h1 policy | linear-mag lerp; InterpToDC | single constants (`LowEndPolicy`) |
| Loudness normalization across scan | peak = 0 dB | one function; white-noise loudness measurement |
| Scan morph domain (linear vs dB vs complex) | linear-mag (spectral) / complex (cyclic) | one lerp site + compile switch |
| Update cadence / fade length (zipper vs smear) | tick = 2.67 ms | `EngineConfig` + zipper test |
| LINEAR window α vs notch depth | Tukey 0.25 | notch-depth regression test |
| Kernel-gen cost @192 kHz small buffers | inline | pre-designed worker escape hatch; halve update rate fallback |

## Sources
- [Kilohearts Filter Table product page](https://kilohearts.com/products/filter_table) · [Filter Table docs](https://kilohearts.com/docs/filter_table) · [launch blog](https://kilohearts.com/blog/introducing_kilohearts_filter_table)
- [Kilohearts wavetable docs](https://kilohearts.com/docs/wavetables) (2048-sample frames; native tables 256 frames, WAV/FLAC)
- [KVR product listing](https://www.kvraudio.com/product/filter-table-by-kilohearts) (formats, platforms, version)
