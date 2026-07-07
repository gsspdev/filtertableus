// FilterTableUS wavetable I/O — shared per-frame post-processing and resampling helpers.
// Owned by the wavetable workstream. Used by WavImporter, SampleConverter and
// FactoryGenerators so every path applies IDENTICAL, deterministic frame conditioning:
//   removeDc -> normalizePeak(0.9) -> wrapCrossfade(last 16 samples into the first 16).
//
// Threading: pure functions, no allocation except evenStrideIndices; safe on any thread.
#pragma once
#include <span>
#include <vector>

namespace ftus::wtio {

inline constexpr int kFrameLength = 2048;     // == ftc::WavetableData::kFrameLength
inline constexpr int kMaxFrames = 256;        // == ftc::WavetableData::kMaxFrames
inline constexpr int kWrapFadeLength = 16;    // samples blended across the loop seam
inline constexpr float kNormalizePeak = 0.9f; // per-frame peak target

/// Subtract the frame mean.
void removeDc(std::span<float> frame) noexcept;

/// Scale so the absolute peak hits targetPeak. Near-silent frames are left untouched
/// (never divides by ~0, never produces NaN/inf).
void normalizePeak(std::span<float> frame, float targetPeak = kNormalizePeak) noexcept;

/// Crossfade the last `fadeLength` samples into the first `fadeLength`'s content:
///   out[N-fade+i] = (1-w)*x[N-fade+i] + w*x[i],  w: 0 -> 1 across the fade.
/// The fade starts transparently (w=0 at sample N-fade) and lands exactly on x[fade-1]
/// at the last sample, so the wrap step shrinks to the head's slope over `fadeLength`
/// samples and any discontinuity near the frame end (e.g. a resampled saw edge) is
/// smeared across the fade — kills Raw-mode clicks.
void wrapCrossfade(std::span<float> frame, int fadeLength = kWrapFadeLength) noexcept;

/// The full import conditioning chain: removeDc -> normalizePeak -> wrapCrossfade.
void postProcessFrame(std::span<float> frame) noexcept;

/// Catmull-Rom interpolation at fractional position p with edge-clamped indexing.
float catmullRomClamped(std::span<const float> src, double p) noexcept;

/// Catmull-Rom interpolation at fractional position p, indices wrapped mod src.size().
float catmullRomCircular(std::span<const float> src, double p) noexcept;

/// Treat src as exactly one cycle and resample it circularly to dst.size() samples.
void resampleCycleCircular(std::span<const float> src, std::span<float> dst) noexcept;

/// `count` indices evenly strided over [0, total-1], always keeping both endpoints.
/// Requires 1 <= count <= total. count == 1 returns {0}.
std::vector<int> evenStrideIndices(int total, int count);

} // namespace ftus::wtio
