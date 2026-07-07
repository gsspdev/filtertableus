// Shared helpers for the kernel-generation test suite (test_kernel_*.cpp).
#pragma once
#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

#include "ftc/EngineConfig.h"
#include "ftc/Kernel.h"
#include "ftc/KernelGenerator.h"
#include "ftc/WavetableData.h"
#include "helpers/TestSignals.h"

namespace ftk {

/// Prepared generator + kernel for one sample rate (L and N from EngineConfig).
struct GenFixture {
    double fs;
    int L;
    int N;
    ftc::KernelGenerator gen;
    ftc::Kernel kernel;

    explicit GenFixture(double sampleRate)
        : fs(sampleRate),
          L(ftc::EngineConfig::kernelLength(sampleRate)),
          N(ftc::EngineConfig::designFftSize(sampleRate)) {
        ftc::KernelGenerator::Config cfg;
        cfg.sampleRate = fs;
        cfg.kernelLength = L;
        gen.prepare(cfg);
        kernel.prepare(L);
    }

    ftc::Kernel& make(const ftc::WavetableData* table, ftc::PhaseMode mode, float scan,
                      float cutoffHz, float resonance) {
        gen.setWavetable(table);
        ftc::KernelRequest rq;
        rq.mode = mode;
        rq.scan = scan;
        rq.cutoffHz = cutoffHz;
        rq.resonance = resonance;
        gen.generate(rq, kernel);
        return kernel;
    }

    /// |FFT(kernel zero-padded to N)| — design-grid magnitude response of the last kernel.
    std::vector<float> response() const { return ftt::measureMagnitudeResponse(kernel.taps(), N); }

    int binOf(double hz) const { return static_cast<int>(std::lround(hz * N / fs)); }
    double exactBinOf(double hz) const { return hz * N / fs; }
};

/// One 2048-sample frame from a list of (harmonic, amplitude) partials.
inline std::vector<float> makeHarmonicFrame(const std::vector<std::pair<int, float>>& partials) {
    std::vector<float> v(static_cast<size_t>(ftt::kFrameLength), 0.0f);
    constexpr double twoPi = 6.283185307179586476925286766559;
    for (const auto& [harmonic, amp] : partials)
        for (int n = 0; n < ftt::kFrameLength; ++n)
            v[static_cast<size_t>(n)] += amp
                * static_cast<float>(std::sin(twoPi * harmonic * n / ftt::kFrameLength));
    return v;
}

inline float dB(float linear) { return 20.0f * std::log10(linear + 1e-12f); }

inline int argmax(std::span<const float> v) {
    int best = 0;
    for (int i = 1; i < static_cast<int>(v.size()); ++i)
        if (v[static_cast<size_t>(i)] > v[static_cast<size_t>(best)])
            best = i;
    return best;
}

inline float peakDb(std::span<const float> response) {
    float p = 0.0f;
    for (float m : response)
        p = std::max(p, m);
    return dB(p);
}

/// Response in dB at the design bin nearest to hz.
inline float responseDbAt(const std::vector<float>& response, double fs, int nfft, double hz) {
    int bin = static_cast<int>(std::lround(hz * nfft / fs));
    bin = std::clamp(bin, 0, static_cast<int>(response.size()) - 1);
    return dB(response[static_cast<size_t>(bin)]);
}

/// Max response in dB over bins [center-radius, center+radius] around hz.
inline float maxResponseDbNear(const std::vector<float>& response, double fs, int nfft,
                               double hz, int radius) {
    const int center = static_cast<int>(std::lround(hz * nfft / fs));
    float p = 0.0f;
    for (int b = center - radius; b <= center + radius; ++b)
        if (b >= 0 && b < static_cast<int>(response.size()))
            p = std::max(p, response[static_cast<size_t>(b)]);
    return dB(p);
}

inline bool allFinite(std::span<const float> v) {
    for (float x : v)
        if (!std::isfinite(x))
            return false;
    return true;
}

inline double totalEnergy(std::span<const float> v) {
    double e = 0.0;
    for (float x : v)
        e += static_cast<double>(x) * static_cast<double>(x);
    return e;
}

inline double energyOfFirst(std::span<const float> v, int count) {
    double e = 0.0;
    for (int i = 0; i < count && i < static_cast<int>(v.size()); ++i)
        e += static_cast<double>(v[static_cast<size_t>(i)])
             * static_cast<double>(v[static_cast<size_t>(i)]);
    return e;
}

inline double energyCentroid(std::span<const float> v) {
    double num = 0.0, den = 0.0;
    for (int i = 0; i < static_cast<int>(v.size()); ++i) {
        const double e = static_cast<double>(v[static_cast<size_t>(i)])
                         * static_cast<double>(v[static_cast<size_t>(i)]);
        num += e * i;
        den += e;
    }
    return den > 0.0 ? num / den : 0.0;
}

} // namespace ftk
