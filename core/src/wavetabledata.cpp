#include "ftc/WavetableData.h"

#include <cmath>

#include "ftc/FFT.h"

namespace ftc {

std::shared_ptr<const WavetableData> WavetableData::analyze(std::span<const float> frames,
                                                            int numFrames,
                                                            std::string name) {
    if (numFrames < 1 || numFrames > kMaxFrames)
        return nullptr;
    if (frames.size() != static_cast<size_t>(numFrames) * kFrameLength)
        return nullptr;

    auto data = std::shared_ptr<WavetableData>(new WavetableData());
    data->numFrames_ = numFrames;
    data->name_ = std::move(name);
    data->samples_.assign(frames.begin(), frames.end());
    data->magnitudes_.resize(static_cast<size_t>(numFrames) * kNumBins);
    data->spectra_.resize(static_cast<size_t>(numFrames) * kNumBins);

    RealFFT fft(kFrameLength);
    float maxMag = 0.0f;
    for (int f = 0; f < numFrames; ++f) {
        const float* time = data->samples_.data() + static_cast<size_t>(f) * kFrameLength;
        std::complex<float>* spec = data->spectra_.data() + static_cast<size_t>(f) * kNumBins;
        float* mags = data->magnitudes_.data() + static_cast<size_t>(f) * kNumBins;
        fft.forward(time, spec);
        for (int k = 0; k < kNumBins; ++k) {
            const float m = std::abs(spec[k]);
            mags[k] = m;
            if (k > 0 && m > maxMag) // table-wide max excludes DC
                maxMag = m;
        }
    }
    data->maxMagnitude_ = maxMag;
    return data;
}

std::span<const float> WavetableData::frame(int i) const noexcept {
    i = i < 0 ? 0 : (i >= numFrames_ ? numFrames_ - 1 : i);
    return {samples_.data() + static_cast<size_t>(i) * kFrameLength, kFrameLength};
}

std::span<const float> WavetableData::magnitudes(int i) const noexcept {
    i = i < 0 ? 0 : (i >= numFrames_ ? numFrames_ - 1 : i);
    return {magnitudes_.data() + static_cast<size_t>(i) * kNumBins, kNumBins};
}

std::span<const std::complex<float>> WavetableData::spectrum(int i) const noexcept {
    i = i < 0 ? 0 : (i >= numFrames_ ? numFrames_ - 1 : i);
    return {spectra_.data() + static_cast<size_t>(i) * kNumBins, kNumBins};
}

} // namespace ftc
