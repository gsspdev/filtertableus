# Agent A1 — Convolution engine

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan sections: §2 "Convolution & time variation" + EngineConfig constants.

## Owns (exclusive)
- `core/src/convolver*.{h,cpp}` (implementation files for `ftc::KernelImage`, `ftc::PartitionedConvolver`, `ftc::ConvolutionSection` declared in `core/include/ftc/Convolver.h`)
- `core/tests/test_convolver*.cpp`
- Build dir: `build/a1` (core-only: configure with `-DFTUS_BUILD_PLUGIN=OFF` — no JUCE in your loop)

## What to build
**`PartitionedConvolver`** (mono, zero algorithmic latency, per plan §2):
- Head: first `headLength` (128) taps as a direct FIR over a circular input history — per-sample output, zero latency. Write it so the compiler can vectorize (contiguous arrays); correctness first, then speed.
- Tail: remaining taps in uniform partitions of `partitionLength` (128), zero-padded to 256-point real FFTs (`ftc::RealFFT`, pffft z-domain layout + `zconvolveAccumulate`), classic frequency-domain delay line (FDL) of input-block spectra. Tail partition j covers lags [128+j·128, 128+(j+1)·128) and therefore needs only COMPLETED past input blocks — no added latency (Gardner scheduling). Get the bookkeeping right for arbitrary call sizes n ≥ 1: internal 128-sample accumulation FIFO, output = head contribution (per sample) + precomputed tail contribution.
- `analyze(const Kernel&, KernelImage&)`: RT-safe (no alloc) — copy head taps + forward-FFT each 128-tap partition into the image's z-domain spectra. `KernelImage::prepare` allocates worst case (maxKernelLength).
- `copyStateFrom(other)`: bounded memcpy of FDL contents + head history + FIFO positions so a freshly-loaded convolver continues the other's stream seamlessly.
- `reset()`: zero all state.
**`ConvolutionSection`** (multi-channel wrapper): per channel two PartitionedConvolver instances (A/B) + shared KernelImage slots (active/incoming/spare — images are channel-independent). `pushKernel(kernel)`: if a fade is in flight return false; else analyze into the spare image, copyStateFrom(active) into the idle convolver, start a per-sample **linear** output crossfade over `fadeLengthSamples`; on completion swap roles, idle instance stops processing (steady-state cost 1×). `setKernelImmediate`: hard swap + reset both (used by mode switches; caller masks with its own fade). `process(float* const* channels, int n)`: in-place wet. `currentLatencySamples()`: the active image's latency.

## Acceptance (all must pass; write these as Catch2 tests using `core/tests/helpers/` naive-convolution reference)
1. Random kernel (lengths 129, 1000, 2048) × random input vs naive O(n·L) convolution, tolerance 1e-5 relative, driven with block-size sequences {1}, {17}, {64}, {441}, {4096}, and a mixed sequence {1,17,64,441,3,128}.
2. Delta kernel (h[0]=1) → output ≡ input at ZERO sample offset (the zero-latency proof).
3. `copyStateFrom` mid-stream → continuation bit-identical to uninterrupted processing.
4. Crossfade: two kernels A→B mid-stream — output during the fade equals (1−t)·convA + t·convB within 1e-5; after fade, identical to B-only steady state; click detector (helpers) finds no discontinuity.
5. `reset()` → subsequent output identical to a freshly-prepared instance.
6. No allocation after prepare (if the scaffold provides an allocation-counting hook use it; otherwise assert preallocated capacities are never exceeded).
7. Optional benchmark test tagged `[.perf]`: report throughput at L=2048, fs-equivalent block 512.
