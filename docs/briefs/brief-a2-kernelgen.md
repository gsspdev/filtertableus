# Agent A2 — Kernel generation (analysis, response construction, 4 phase modes)

Read `/Users/music/.claude/jobs/39205b91/tmp/brief-wave1-common.md` first. Plan sections: §2 "Analysis", "Response construction", "Kernel reconstruction", §9 risk register.

## Owns (exclusive)
- `core/src/kernel*.{h,cpp}` (implementation of `ftc::KernelGenerator`, `ftc::Kernel` internals if needed)
- `core/src/wavetabledata.cpp` (Phase 0 shipped a working `WavetableData::analyze` — you own/extend it; keep magnitudes + complex spectra + raw frames accessible per the frozen header)
- `core/tests/test_kernel*.cpp`
- Build dir: `build/a2` (core-only: `-DFTUS_BUILD_PLUGIN=OFF`)

## What to build (`KernelGenerator::generate(request, out)` — RT-safe, zero alloc after prepare)
Two paths by mode:
**Spectral path (Minimum, Linear)**
1. Scan: framePos = scan·(F−1), lerp the two adjacent frames' **linear magnitude** arrays (1025 bins). Keep the lerp in ONE inline function with a compile-time switch for dB-domain lerp (`FTUS_SCAN_LERP_DB`) — calibrate-by-ear item.
2. Resonance (r ∈ −1..+1): k_r = 4^r; in dB domain about the in-band mean: L'db = Lref + k_r·(Ldb − Lref); floor at (max − 120 dB). One function `applyResonance()` — highest-risk calibration item, keep it isolated.
3. Cutoff mapping onto the design grid (N = 4·L bins): for output bin k at f_k = k·fs/N, continuous harmonic index h = 24·f_k/fc. 1≤h≤1024: linear interpolation between adjacent harmonic magnitudes. h<1: interpolate toward the frame's DC bin value (`LowEndPolicy::InterpToDC` default; `HoldH1` as compile-time alternative). h>1024: 0, with a short half-cosine taper over the last ~8 harmonics before the edge (no brickwall ringing). Use `FastMath` vector helpers for dB↔linear over whole arrays.
4. Normalize peak magnitude to 0 dB.
5. Minimum: real-cepstrum minimum phase — ln M (floored) → inverse real FFT → causal fold (c[0], 2·c[1..N/2−1], c[N/2], zeros) → forward FFT → complex exp (`FastMath::expComplex`, clamp Re ≤ +10) → inverse FFT → truncate to L taps → half-Hann fade over the last L/8. Latency 0.
   Linear: zero-phase inverse FFT of M → circular rotate by L/2 → truncate to L → symmetric Tukey(0.25) window. Latency L/2 (exact tap symmetry).
**Cyclic path (Original, Raw)**
1. Scan: lerp the two frames' **complex spectra** (1025 bins).
2. Resonance applied to |S| with phases untouched (same `applyResonance`).
3. Original: zero bins whose mapped frequency h·fc/24 > fs/2 (anti-alias) → inverse 2048-pt FFT = one cycle → circular Catmull-Rom resample so one cycle spans P₀ = 24·fs/fc output samples (read circularly; if P₀ > L wrap-repeat the periodic waveform to fill L) → center at L/2, Tukey window over its extent → latency L/2 CONSTANT (never varies with cutoff).
   Raw: same but NO band-limit, NO window, causal from tap 0, hard truncate at L, energy normalization Σh² = 1. Latency 0.
`latencyForMode`: Minimum/Raw → 0; Linear/Original → L/2. All FFT plans + scratch allocated in `prepare` (design FFT N = 4·L; also the 2048-pt cyclic FFT).

## Acceptance (Catch2, using `core/tests/helpers/` analytic frames + `measureMagnitudeResponse`)
1. Single-harmonic-24 frame, fc = 1 kHz, res 0 → measured magnitude peak at 1.0 kHz ± 1 design bin, for Minimum AND Linear, at fs ∈ {44100, 48000, 96000}.
2. Harmonic-1 frame → peak at fc/24. Saw frame → response trend ≈ −6 dB/oct sampled at harmonic centers (tolerance 1.5 dB).
3. Minimum: in-band |FFT(kernel)| within ±1 dB of target where target > −60 dB; cumulative energy ≥ 70% in the first L/8 taps (saw frame) — regression bound.
4. Linear: taps[i] == taps[L−1−i] within 1e-6; latency L/2.
5. Original: sine-at-harmonic-1 frame → comb fundamental at fc/24; no content above Nyquist at high fc (band-limit works); latency L/2 at fc ∈ {100, 1000, 10000}.
6. Raw: latency 0 (first tap energy), energy normalized.
7. Resonance: r=+1 deepens a known trough and sharpens a known peak monotonically vs r=0; r=−1 flattens toward uniform; output kernel peak magnitude ∈ [−1, +0.5] dB across all modes/frames/cutoffs tested.
8. Two-frame morph table at scan 0.5 → response between the endpoints; scan sweep 0→1 in 32 steps → response varies continuously (no jumps > a sane bound between adjacent steps).
9. Robustness: fc = 20 Hz and 20 kHz don't produce NaN/inf; 1-frame table degenerates safely; all-silent frame → floored response, no NaN.
