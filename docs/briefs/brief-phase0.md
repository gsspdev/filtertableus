# Phase 0 scaffold brief — FilterTableUS

You are the **Phase 0 scaffold agent** for FilterTableUS — a from-scratch clone of the Kilohearts "Filter Table" plugin (wavetable frames → filter frequency responses, scanned/morphed) built in `/Users/music/Developer/filtertableus` (git repo, currently empty except `.claude/` and `external/`; two previous scaffold attempts stalled on network calls before writing anything — if you find partial files, reconcile or overwrite them). Machine: macOS 13 arm64, Xcode 15.2, CMake 3.31. If `ninja` is missing, fall back to the Xcode or "Unix Makefiles" generator and note it.

**NETWORK POLICY (a previous run hung on a network fetch)**: all git dependencies are ALREADY CLONED locally under `external/` (see below) — do not clone or `git ls-remote` anything. For the two remaining downloads (AGPL license text, pluginval binary) use `curl --max-time 120 --retry 2`; if a download fails, don't block: write a placeholder + prominent note in your report (LICENSE placeholder = "AGPL-3.0-or-later — canonical text to be added") and for pluginval mark the validation step as NOT RUN in your report.

**Local dependency checkouts in `external/` (pinned; see `external/PINS.txt`)**: JUCE `8.0.14` (2cdfca8), clap-juce-extensions 16e9d4c (submodules initialized), Catch2 v3.7.1, pffft a4b0359. Add `external/` and `build/` to `.gitignore`.

**FIRST**: Read the full plan at `/Users/music/.claude/plans/make-a-plan-to-crispy-hopcroft.md` — the authoritative spec (§1 repo layout/build, §2 DSP core design, §3 parameters/state, §4 wavetable I/O, §5 GUI, §6 threading, §7 your Phase 0 checklist). This brief adds exact API baselines and practical notes.

Your job is Phase 0 ONLY: build system + vendored deps + core foundation + ALL frozen interface headers + plugin shell wiring + compiling stubs + validation. After you, six agents work in parallel against your frozen headers — everything must compile, validate, and be unambiguous. Do NOT implement the real DSP (convolver, kernel generator, modulators, GUI, loaders) — ship stubs for those.

**Practical note on long builds**: a universal (arm64+x86_64) JUCE plugin build can exceed the 10-minute foreground command limit. Run long builds as background commands and wait for completion, or split the build into targets (build `ftcore` and tests first, then the plugin target).

## Deliverables

### 1. Build system
- Root `CMakeLists.txt`: `cmake_minimum_required(3.24)`; set `CMAKE_OSX_ARCHITECTURES "arm64;x86_64"` and `CMAKE_OSX_DEPLOYMENT_TARGET "10.13"` as CACHE defaults **before** `project(FilterTableUS VERSION 0.1.0 LANGUAGES C CXX)`; C++20 required; options `FTUS_BUILD_TESTS` (ON) and `FTUS_BUILD_PLUGIN` (ON). **When `FTUS_BUILD_PLUGIN=OFF`, configure must not fetch or touch JUCE/clap-juce-extensions at all** (core agents iterate JUCE-free: only ftcore + Catch2 tests).
- `cmake/Dependencies.cmake`: **prefer the local checkouts** — for each of JUCE / clap-juce-extensions / Catch2: `if(EXISTS ${CMAKE_SOURCE_DIR}/external/<name>/CMakeLists.txt)` → `add_subdirectory(external/<name> ${CMAKE_BINARY_DIR}/_deps/<name>-build EXCLUDE_FROM_ALL)` (Catch2 without EXCLUDE_FROM_ALL if that breaks catch_discover_tests; also `list(APPEND CMAKE_MODULE_PATH .../Catch2/extras)`), `else()` → FetchContent fallback pinned to the shas recorded in `external/PINS.txt` (copy them into comments). This makes all six parallel build dirs share one source checkout with zero network. JUCE and clap-juce-extensions are only referenced when `FTUS_BUILD_PLUGIN=ON`; Catch2 only when `FTUS_BUILD_TESTS=ON`.
- `cmake/Warnings.cmake`: INTERFACE target `ftus::warnings` — clang `-Wall -Wextra -Wshadow -Wdouble-promotion -Wreorder -Werror`, applied ONLY to our targets (never JUCE/deps). No `-ffast-math` anywhere; `-fno-math-errno -ffp-contract=fast` are fine on ftcore.
- `cmake/PluginSetup.cmake`: `juce_add_plugin(FilterTableUS COMPANY_NAME "FilterTableUS Project" BUNDLE_ID "com.filtertableus.FilterTableUS" PLUGIN_MANUFACTURER_CODE FtUs PLUGIN_CODE Ftbl FORMATS VST3 AU Standalone PRODUCT_NAME "FilterTableUS" IS_SYNTH FALSE NEEDS_MIDI_INPUT TRUE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE AU_MAIN_TYPE kAudioUnitType_MusicEffect VST3_CATEGORIES Fx Filter COPY_PLUGIN_AFTER_BUILD TRUE MICROPHONE_PERMISSION_ENABLED TRUE)` + `clap_juce_extensions_plugin(TARGET FilterTableUS CLAP_ID "com.filtertableus.filtertable" CLAP_FEATURES audio-effect filter stereo)`. Compile definitions `JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_DISPLAY_SPLASH_SCREEN=0`. Link modules: juce_audio_utils, juce_dsp, juce_audio_formats, juce_gui_extra. Two binary-data targets with one placeholder file each: `ftus_assets` (resources/fonts) and `ftus_presets` (resources/presets/factory).
- **Source-ownership scheme**: the plugin target's sources are added ONLY via per-directory `CMakeLists.txt` (`source/plugin/`, `source/wavetable/`, `source/gui/`, `source/state/`) each calling `target_sources(FilterTableUS PRIVATE …)`; `core/CMakeLists.txt` defines static lib `ftcore` (JUCE-free — must not include any JUCE header) listing its sources; `core/tests/CMakeLists.txt` and `tests/` define Catch2 test executables registered with ctest (`catch_discover_tests`). Tests split: `ftcore_tests` (JUCE-free, links ftcore only), plus placeholder unit test dirs `tests/unit/{wavetable,state}` and `tests/integration/` (these link the plugin code — for Phase 0 a trivial compile-and-pass test each; keep the integration target buildable headless).
- `.gitignore` (build/, .DS_Store, .cache, etc.), `README.md` (name, one-paragraph description, build commands, license note), `LICENSE` = canonical AGPL-3.0 text (fetch from https://www.gnu.org/licenses/agpl-3.0.txt).

### 2. Vendored pffft
Copy from the local checkout `external/pffft/` (do NOT clone anything) the minimal float-precision files (pffft.h, pffft.c + the companion simd dispatch headers they include) into `core/third_party/pffft/` together with its LICENSE file; record the source commit sha (a4b0359, from external/PINS.txt) in a README.md there. Compile as part of ftcore (C; no warnings-as-errors on third_party).

### 3. Core foundation — IMPLEMENTED + TESTED (namespace `ftc`, JUCE-free)
- `ftc/FFT.h/.cpp` — `RealFFT` wrapping pffft: ctor(size) (validate pffft-legal sizes: 2^a, ≥32 for real), `forward(const float* time, std::complex<float>* spec)` (N/2+1 bins), `inverse` (scaled 1/N), `forwardZ(const float*, float* z)`, `zconvolveAccumulate(zA, zB, zAcc, scale)`, `inverseZ`. Movable, non-copyable. Uses pffft aligned work buffers internally.
- `ftc/FastMath.h/.cpp` — vectorizable `logAbsApprox(const float* in, float* out, int n)` (= ln(max(|x|, 1e-6))), `expApprox(const float* in, float* out, int n)`, `expComplex(const std::complex<float>* lnH, std::complex<float>* H, int n)` (e^re·(cos im, sin im)). Plain loops the compiler can auto-vectorize are fine; accuracy: relative error < 1e-4 over sensible domains — tested.
- `ftc/AlignedVector.h` — 64-byte-aligned `std::vector` alias via a minimal aligned allocator.
- `ftc/Denormals.h/.cpp` — `ScopedNoDenormals` setting FTZ/DAZ via MXCSR (x86-64) / FPCR.FZ (arm64), restoring on destruction.
- `ftc/RealtimeExchange.h` — `template <class T> class TripleBuffer` (trivially-copyable snapshots; `write(const T&) noexcept` producer, `bool read(T&) noexcept` wait-free consumer; 3 slots + one atomic index word, tear-free) and `template <class T> class ObjectHandoff` (`publish(std::shared_ptr<const T>)` message thread; `const T* acquire() noexcept` wait-free audio thread; `collectGarbage()` message thread frees entries no longer reachable as pending/current/previous — the audio thread must never run a destructor). NOTE: `std::atomic<std::shared_ptr>` is BANNED (not in Xcode 15 libc++) — use raw-pointer atomics + a message-thread-owned retention list.
- `ftc/EngineConfig.h` — constants from the plan: control interval 64; head 128; partition 128; `int kernelLength(double fs)` → 2048 (fs≤50k) / 4096 (≤100k) / 8192; `int kernelUpdateInterval(double fs)` → 128/256/512; smoothing time constants (15 ms scan/res/log2-cutoff, 10 ms mix/gain); −120 dB floor; crossfade = one kernel update interval.
- **Tests** (Catch2, `ftcore_tests`): RealFFT round-trip + Parseval + cross-check vs naive O(n²) DFT at sizes 32/64/128; zconvolveAccumulate equals ordered complex multiply-accumulate; FastMath accuracy bounds; TripleBuffer/ObjectHandoff basic two-thread hammer (bounded iterations); ScopedNoDenormals flushes a denormal multiply to zero.

### 4. Frozen interface headers (full doc comments incl. thread contract per method)
`core/include/ftc/` — exact baseline (refine signatures if something can't compile as-is, but keep names/semantics; document any change in docs/INTERFACES.md):
```cpp
// Types.h
namespace ftc {
enum class PhaseMode : int { Minimum, Linear, Original, Raw };
enum class LfoShape : int { Sine, Triangle, SawUp, SawDown, Square, SampleHold };
enum class SyncDivision : int { W8, W4, W2, W1, Half, HalfT, QuarterD, Quarter, QuarterT,
                                EighthD, Eighth, EighthT, SixteenthD, Sixteenth, SixteenthT, ThirtySecond }; // 16
struct TransportInfo { double bpm=120.0, ppqPosition=0.0; bool playing=false, valid=false; };
struct NoteEvent { int sampleOffset; std::uint8_t note, velocity; bool noteOn; }; }
// Parameters.h — trivially copyable POD mirroring the host params (plan §3)
struct LfoParams { LfoShape shape; bool tempoSync; float rateHz; SyncDivision division; bool retrigger; float toScan, toCutoff; };
struct EnvParams { float sensitivityDb, attackMs, releaseMs, toScan, toCutoff; };
struct Parameters { float scan=0, cutoffHz=440, resonance=0 /* -1..+1 */, mix=1;
  PhaseMode mode=PhaseMode::Minimum; float keytrack=0 /* ±1 st per st from A4=69 */, outGainDb=0;
  LfoParams lfo1{LfoShape::Sine,false,1.0f,SyncDivision::Quarter,false,0,0};
  LfoParams lfo2{LfoShape::Sine,false,0.25f,SyncDivision::Quarter,false,0,0};
  EnvParams env{0,10,200,0,0}; };
// WavetableData.h — immutable analyzed table
class WavetableData { public:
  static constexpr int kFrameLength=2048, kNumBins=1025, kMaxFrames=256;
  static std::shared_ptr<const WavetableData> analyze(std::span<const float> frames, int numFrames, std::string name); // allocates; message/loader thread ONLY
  int numFrames() const noexcept;
  std::span<const float> frame(int i) const noexcept;                       // 2048 raw samples
  std::span<const float> magnitudes(int i) const noexcept;                  // 1025 linear magnitudes
  std::span<const std::complex<float>> spectrum(int i) const noexcept;      // 1025 complex bins
  const std::string& name() const noexcept; };
// Kernel.h
struct KernelRequest { PhaseMode mode; float scan, cutoffHz, resonance; };
class Kernel { public: void prepare(int maxLength); float* data() noexcept; std::span<const float> taps() const noexcept;
  void setLength(int) noexcept; int length() const noexcept; void setLatency(int) noexcept; int latencySamples() const noexcept; };
// KernelGenerator.h
class KernelGenerator { public: struct Config { double sampleRate; int kernelLength; };
  void prepare(const Config&);                                  // allocates FFT plans/scratch
  void setWavetable(const WavetableData*) noexcept;             // non-owning, RT-safe
  void generate(const KernelRequest&, Kernel& out) noexcept;    // RT-safe, no alloc
  static int latencyForMode(PhaseMode, int kernelLength) noexcept; }; // 0 or L/2
// Convolver.h
class KernelImage { public: void prepare(int maxKernelLength, int headLen, int partLen); int latencySamples() const noexcept; /* + accessors the convolver needs */ };
class PartitionedConvolver { public: struct Config { int maxKernelLength, headLength, partitionLength, maxBlockSize; };
  void prepare(const Config&); void analyze(const Kernel&, KernelImage&) const noexcept; void reset() noexcept;
  void copyStateFrom(const PartitionedConvolver&) noexcept;
  void process(const KernelImage&, const float* in, float* out, int n) noexcept; };
class ConvolutionSection { public: void prepare(const PartitionedConvolver::Config&, int numChannels, int fadeLengthSamples);
  void reset() noexcept; bool pushKernel(const Kernel&) noexcept; void setKernelImmediate(const Kernel&) noexcept;
  bool isFading() const noexcept; void process(float* const* channels, int n) noexcept; int currentLatencySamples() const noexcept; };
// Modulation.h — fixed routing (2 LFOs + env + keytrack → scan/cutoff)
struct ModValues { float scanOffset; float cutoffSemis; }; // summed incl. keytrack; depths: toScan ±1=full range, toCutoff ±1=±48 st, keytrack 1 st/st from note 69
class ModulationEngine { public: void prepare(double fs, int controlInterval);
  void setParams(const Parameters&) noexcept;
  void beginBlock(const TransportInfo&, std::span<const NoteEvent>, const float* monoIn, int n) noexcept;
  ModValues evaluate(int subBlockOffset) noexcept;              // per 64-sample control tick
  float envValue() const noexcept; };                           // 0..1 UI meter
// ResponseCurve.h
struct ResponseCurve { static constexpr int kNumPoints=256; static constexpr float kMinHz=20.f, kMaxHz=20000.f;
  static float frequencyForPoint(int i) noexcept;               // log-spaced; SINGLE definition GUI+engine share
  std::array<float, kNumPoints> db; };
// FilterTableEngine.h — the facade (stub-swappable .cpp)
class FilterTableEngine { public: struct PrepareSpec { double sampleRate; int maxBlockSize, numChannels; };
  FilterTableEngine(); ~FilterTableEngine();
  void prepare(const PrepareSpec&); void reset() noexcept;
  void setParameters(const Parameters&) noexcept;               // RT-safe POD copy, audio thread at block start
  void setWavetable(std::shared_ptr<const WavetableData>);      // message thread
  void process(float* const* channels, int numChannels, int numSamples,
               const TransportInfo&, std::span<const NoteEvent>) noexcept;  // audio thread
  int latencySamples() const noexcept; static int latencySamplesFor(PhaseMode, double fs) noexcept;
  void collectGarbage();                                        // message thread, ~1 Hz
  bool readResponseCurve(ResponseCurve&) noexcept;              // GUI timer
  float currentScan() const noexcept; float envValue() const noexcept; };
```
`include/ftus/` (JUCE allowed here):
- `PluginIDs.h` — ALL 27 param IDs exactly per plan §3 table (`scan,cutoff,resonance,mix,phaseMode,keytrack,outGain,bypass, lfo1Rate,lfo1Sync,lfo1Div,lfo1Shape,lfo1Retrig,lfo1ToScan,lfo1ToCutoff, lfo2… (same 7), envSens,envAttack,envRelease,envToScan,envToCutoff`) as constexpr IDs + ranges/defaults/skews + choice-string arrays (phase modes, 6 LFO shapes, 16 sync divisions matching `ftc::SyncDivision` order) + `versionHint=1` + a declared `createParameterLayout()`.
- `WavetableCodec.h` — `struct TableSourceInfo { enum class Type { Factory, UserFile, Converted }; Type type; juce::String factoryId, path, displayName; };` + `juce::ValueTree encodeWavetable(const ftc::WavetableData&, const TableSourceInfo&)` / `struct DecodedTable { std::shared_ptr<const ftc::WavetableData> table; TableSourceInfo info; }; std::optional<DecodedTable> decodeWavetable(const juce::ValueTree&)` — gzip'd float32-LE base64 per plan §3. **Implement fully in Phase 0** (`source/plugin/WavetableCodecImpl.cpp`) + round-trip unit test.
- `FactoryTables.h` — `enum class FactoryTableId` (AnalogMorph, Pwm, VowelMorph, CombSweep, NotchArray, HarmonicLadder, OddEvenMorph, SpectralDrift, FormantPeaks, DigitalSteps, MetalCluster, SubBloom) + display names + `struct RawTable { int numFrames; std::vector<float> samples; std::string name; }` + declared `RawTable generateFactoryTable(FactoryTableId)`.
- `LoaderService.h` — abstract: `void requestLoadFile(const juce::File&)`, `void requestFactoryTable(FactoryTableId)`, `float progress() const`, result delivery on the message thread via `std::function<void(LoadResult)>` where `LoadResult { bool ok; juce::String errorMessage; std::shared_ptr<const ftc::WavetableData> table; TableSourceInfo info; }`.
- `StateManager.h` — abstract: `void getState(juce::MemoryBlock&)`, `void setState(const void*, int)`, preset ops (`juce::StringArray listPresets()`, `bool loadPreset(name)`, `bool saveUserPreset(name)`, `nextPreset()/prevPreset()`, `juce::String currentPresetName()`, `bool isDirty()`).
- `docs/INTERFACES.md` — per-header thread contracts + the hard GUI rules from plan §5; `docs/ARCHITECTURE.md` — condensed architecture (summarize from the plan file).

### 5. Plugin shell (`source/plugin/`, frozen after you)
`PluginProcessor`: owns APVTS (layout from PluginIDs), `ftc::FilterTableEngine`, cached raw-value atomics; `prepareToPlay` → engine.prepare + initial `setLatencySamples`; `processBlock` → ScopedNoDenormals, build `ftc::Parameters` from atomics, collect `NoteEvent`s from MidiBuffer, TransportInfo from `getPlayHead()`, `engine.setParameters` + `engine.process`; bypass param returned from `getBypassParameter()`; APVTS listener on `phaseMode` → AsyncUpdater → message-thread `setLatencySamples(engine.latencySamples())` + `updateHostDisplay`; a 1 Hz message-thread timer calling `engine.collectGarbage()`; `getStateInformation/setStateInformation` delegated to the `StateManager` interface (stub impl for now); `hasEditor` true with stub `juce::GenericAudioProcessorEditor` (in `source/gui/PluginEditor.cpp` so the GUI agent owns/replaces that file); buses {mono→mono, stereo→stereo} only.

### 6. Stubs (each in its future owner's directory, compiling & behaving sanely)
- `core/src/engine_stub.cpp` — real `FilterTableEngine` implementation as PASSTHROUGH: latency always 0, stores/keeps the wavetable via ObjectHandoff, publishes a flat 0 dB ResponseCurve, currentScan mirrors setParameters().scan, audio untouched (document it).
- `core/src/mod_stub.cpp` — ModulationEngine returning zeros (envValue 0).
- **Real `WavetableData::analyze`** in `core/src/wavetabledata.cpp` — real, not a stub (per-frame 2048-pt RealFFT → magnitudes + complex spectra; the kernel-generation agent owns/extends the file later).
- `source/wavetable/LoaderServiceStub.cpp` — implements LoaderService: `requestLoadFile` → immediate error callback "loader not implemented yet"; `requestFactoryTable` → also error (wavetable agent replaces).
- `source/state/StateManagerStub.cpp` — APVTS-only state (params round-trip, no wavetable embedding yet; wire the codec call site with a TODO), presets: empty list, no-ops.
- `core/tests/helpers/` — implemented test helpers shared by later agents: analytic frame builders (sine at harmonic h, saw 1/h, square odd 1/h, single-harmonic-24 frame, two-frame morph table), naive O(n²) convolution reference, naive DFT, `measureMagnitudeResponse(kernel taps, nfft, fs)` probe, and a click detector (normalized second difference vs short-term RMS).
- One trivial passing Catch2 test in each of `tests/unit/wavetable`, `tests/unit/state`, `tests/integration` (the integration one instantiates the processor headless with `juce::ScopedJuceInitialiser_GUI`, prepares/processes silence, asserts finite output + latency 0 — the seed of the harness agent's suite).

### 7. Verify (acceptance — all must pass before you finish)
```bash
cmake -B build/scaffold -G Ninja -DCMAKE_BUILD_TYPE=Release        # universal via cached archs
cmake --build build/scaffold        # background if long
ctest --test-dir build/scaffold --output-on-failure
# Core-only path must configure+build with no network/JUCE:
cmake -B build/scaffold-core -G Ninja -DFTUS_BUILD_PLUGIN=OFF -DCMAKE_BUILD_TYPE=Release && cmake --build build/scaffold-core && ctest --test-dir build/scaffold-core --output-on-failure
```
- **pluginval**: download the latest macOS release binary from https://github.com/Tracktion/pluginval/releases into `scripts/.cache/`, write `scripts/validate_vst3.sh` (accepts optional build-dir arg, default `build/scaffold`) running `pluginval --strictness-level 8 --validate <built .vst3>`, run it, must pass.
- **auval**: `scripts/validate_au.sh` — ensure the component is installed (COPY_PLUGIN_AFTER_BUILD → `~/Library/Audio/Plug-Ins/Components`), `killall -9 AudioComponentRegistrar 2>/dev/null || true`, then `auval -strict -v aumf Ftbl FtUs`; run it, must pass.
- `scripts/validate_clap.sh` — run clap-validator if available (try https://github.com/free-audio/clap-validator/releases); else print a skip notice (non-blocking).
- `scripts/smoke_standalone.sh` — launch the Standalone app, sleep 3, `screencapture -x` into `build/artifacts/`, quit it.

## Rules
- Work ONLY inside `/Users/music/Developer/filtertableus`. Do NOT `git commit` (the orchestrator commits). Build dirs under `build/` only.
- ftcore must compile with zero JUCE includes (verify: grep for "juce" under core/include and core/src finds nothing outside comments).
- Keep every header documented (purpose + thread contract per method). These are FROZEN after you.
- If a pinned dependency version doesn't exist or breaks, choose the nearest working one and record it in `cmake/Dependencies.cmake` comments.

## Final report (your last message — the orchestrator reads only this)
Report concisely: resolved dependency versions (JUCE tag, clap-juce-extensions sha, pffft sha, Catch2, pluginval version); exact build/test/validation commands run and outcomes (pluginval strictness reached, auval verdict, clap-validator status); deviations from this brief or the plan (esp. header signature changes); top-level summary of files created; known warts for later agents.
