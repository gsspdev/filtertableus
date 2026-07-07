// FilterTableUS core — immutable analyzed wavetable. FROZEN after Phase 0.
//
// A wavetable is numFrames frames of exactly 2048 samples (one cycle each). analyze() runs
// one real FFT per frame and stores, per frame:
//   - the raw 2048 samples                        (RAW/ORIGINAL cyclic path, GUI waterfall)
//   - 1025 linear magnitudes  |X[0..1024]|        (MINIMUM/LINEAR spectral path)
//   - 1025 complex bins        X[0..1024]         (ORIGINAL/RAW cyclic path)
//
// Threading: analyze() allocates — message/loader thread ONLY. After construction the object
// is immutable; hand it to the audio side via ObjectHandoff<WavetableData>. All accessors are
// RT-safe.
#pragma once
#include <complex>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace ftc {

class WavetableData {
public:
    static constexpr int kFrameLength = 2048;
    static constexpr int kNumBins = kFrameLength / 2 + 1; // 1025: DC .. harmonic 1024
    static constexpr int kMaxFrames = 256;

    /// frames = numFrames * kFrameLength contiguous samples, frame-major.
    /// Requires 1 <= numFrames <= kMaxFrames and frames.size() == numFrames * kFrameLength.
    /// Returns nullptr on invalid input. NOT RT-safe (allocates, runs FFTs).
    static std::shared_ptr<const WavetableData> analyze(std::span<const float> frames,
                                                        int numFrames,
                                                        std::string name);

    int numFrames() const noexcept { return numFrames_; }
    const std::string& name() const noexcept { return name_; }

    std::span<const float> frame(int i) const noexcept;                        // 2048 samples
    std::span<const float> magnitudes(int i) const noexcept;                   // 1025 linear mags
    std::span<const std::complex<float>> spectrum(int i) const noexcept;       // 1025 bins
    float maxMagnitude() const noexcept { return maxMagnitude_; }              // table-wide max

private:
    WavetableData() = default;
    int numFrames_ = 0;
    std::string name_;
    std::vector<float> samples_;                    // [numFrames][2048]
    std::vector<float> magnitudes_;                 // [numFrames][1025]
    std::vector<std::complex<float>> spectra_;      // [numFrames][1025]
    float maxMagnitude_ = 0.0f;
};

using WavetablePtr = std::shared_ptr<const WavetableData>;

} // namespace ftc
