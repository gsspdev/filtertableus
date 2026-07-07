# FilterTableUS — Architecture (condensed)

Full design + rationale: `docs/PLAN.md`. Contracts: `docs/INTERFACES.md`. This file is the
30-second orientation.

**What it is**: wavetable frames (2048 samples, 1–256 frames) are interpreted as filter
frequency responses. Scan morphs through frames; Cutoff maps frame harmonic 24 to a frequency
(so harmonic k sits at k·fc/24); Resonance is a spectral-contrast control; four phase modes
choose the kernel reconstruction (Minimum = cepstral min-phase, 0 latency; Linear = zero-phase,
L/2; Original = frame phases via cyclic resynthesis, L/2; Raw = frame cycle as causal kernel,
0). Internal modulation: 2 tempo-syncable LFOs + envelope follower + MIDI keytrack → Scan and
Cutoff.

**Layers**

```
JUCE shell (source/plugin, frozen)      APVTS(27 params) → ftc::Parameters POD → engine
  seams: createFtusEditor / createLoaderService / createStateManager / adoptWavetable
ftcore (core/, JUCE-free, pffft)        FilterTableEngine facade:
  analysis   WavetableData::analyze     per-frame FFT → magnitudes + complex spectra
  control    ModulationEngine           64-sample ticks; smoothers; change detector
  kernels    KernelGenerator            scan lerp → resonance → harmonic-24 mapping → 4 modes
  filtering  ConvolutionSection         zero-latency head(128)+partitions(128) FIR, A/B xfade
  output     dry delay = reported latency; per-sample mix/gain ramps
  exchange   ObjectHandoff (tables), TripleBuffer (ResponseCurve → GUI), atomics (scan/env)
GUI (source/gui)                        920×620: waterfall (drag=scan), spectrum
                                        (ResponseCurve grid), knobs, phase selector, mod tabs,
                                        preset bar, drag-drop loading
```

**Cadences** (EngineConfig): control tick 64 samples; kernel rebuild+crossfade every
128/256/512 samples (fs ≤50k/≤100k/≤200k); kernel L = 2048/4096/8192; design FFT 4·L;
smoothing 15 ms (scan/res/log2-cutoff), 10 ms ramps (mix/gain); floor −120 dB.

**Latency**: Minimum/Raw 0, Linear/Original L/2 (21.3 ms @48k). Dry path always delayed by the
*reported* latency (Linear nulls against input at mix 50%; Minimum/Raw mid-mix phasing is
intentional, as in the original product). Mode switch = 5 ms wet fade-out → hard swap + dry
retap → fade-in + async host latency renotify.

**Build**: `ftcore` static lib (no JUCE — tests run in seconds); `ftus_shared` INTERFACE target
carries plugin sources + JUCE modules into the plugin AND the Catch2 test executables
(pamplejuce pattern). Deps from local pinned checkouts in `external/`. Formats: VST3, AU
(`aumf Ftbl FtUs` — music effect so hosts deliver MIDI), CLAP (clap-juce-extensions),
Standalone. Validation: `scripts/run_all_checks.sh` (build → ctest → pluginval L8 → auval →
clap-validator → standalone screenshot).
