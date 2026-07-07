// Shared helpers for the FilterTableEngine test suites (engine-assembly workstream).
#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include "ftc/EngineConfig.h"
#include "ftc/FilterTableEngine.h"
#include "ftc/Parameters.h"
#include "ftc/Types.h"
#include "ftc/WavetableData.h"
#include "helpers/TestSignals.h"

namespace ftet {

constexpr double kFs = 48000.0;
constexpr int kL = 2048;   // EngineConfig::kernelLength(48000)
constexpr int kTick = 128; // EngineConfig::kernelUpdateInterval(48000)

// ------------------------------------------------------------------- tables
/// Single impulse frame: |FFT| == 1 in every bin -> the flattest possible table
/// (passthrough response for the spectral path at high cutoff).
inline ftc::WavetablePtr flatTable() {
    std::vector<float> f(static_cast<size_t>(ftc::WavetableData::kFrameLength), 0.0f);
    f[0] = 1.0f;
    return ftc::WavetableData::analyze(f, 1, "flat-impulse");
}

/// Two-frame dark->bright morph table (ftt helper).
inline ftc::WavetablePtr morphTable() {
    const auto v = ftt::makeTwoFrameMorphTable();
    return ftc::WavetableData::analyze(v, 2, "morph");
}

/// Two-frame morph where BOTH frames are broadband (harmonics 1..512 with different tilts),
/// so scan/cutoff sweeps morph timbre without ever collapsing the output to silence.
inline ftc::WavetablePtr fullBandMorphTable() {
    constexpr int kF = ftc::WavetableData::kFrameLength;
    std::vector<float> v(static_cast<size_t>(2 * kF), 0.0f);
    for (int k = 1; k <= 512; ++k) {
        const double a0 = 1.0 / static_cast<double>(k);          // saw tilt
        const double a1 = 0.15 / std::sqrt(static_cast<double>(k)); // brighter tilt
        for (int n = 0; n < kF; ++n) {
            const double s = std::sin(2.0 * 3.14159265358979323846 * k * n / kF);
            v[static_cast<size_t>(n)] += static_cast<float>(a0 * s);
            v[static_cast<size_t>(kF + n)] += static_cast<float>(a1 * s);
        }
    }
    return ftc::WavetableData::analyze(v, 2, "fullband-morph");
}

// ------------------------------------------------------------------ signals
inline std::uint32_t xorshift32(std::uint32_t& s) {
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

/// Deterministic white noise in [-amp, amp].
inline std::vector<float> makeNoise(int n, std::uint32_t seed, float amp = 0.5f) {
    std::vector<float> v(static_cast<size_t>(n));
    std::uint32_t s = seed != 0 ? seed : 1u;
    for (int i = 0; i < n; ++i) {
        const float u = static_cast<float>(xorshift32(s) >> 8) * (1.0f / 16777216.0f);
        v[static_cast<size_t>(i)] = amp * (2.0f * u - 1.0f);
    }
    return v;
}

inline std::vector<float> makeSine(int n, double freqHz, float amp = 0.5f, double fs = kFs) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        v[static_cast<size_t>(i)] = amp
            * static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * freqHz
                                          * static_cast<double>(i) / fs));
    return v;
}

// ------------------------------------------------------------------ metrics
inline float maxAbs(std::span<const float> x) {
    float m = 0.0f;
    for (float v : x)
        m = std::max(m, std::abs(v));
    return m;
}

inline double rms(std::span<const float> x) {
    if (x.empty())
        return 0.0;
    double acc = 0.0;
    for (float v : x)
        acc += static_cast<double>(v) * static_cast<double>(v);
    return std::sqrt(acc / static_cast<double>(x.size()));
}

/// dB relative to full scale (1.0), floored at -200 dB.
inline double dbfs(double level) {
    return 20.0 * std::log10(std::max(level, 1e-10));
}

inline bool allFinite(std::span<const float> x) {
    for (float v : x)
        if (!std::isfinite(v))
            return false;
    return true;
}

inline int firstIndexAbove(std::span<const float> x, float thresh) {
    for (size_t i = 0; i < x.size(); ++i)
        if (std::abs(x[i]) > thresh)
            return static_cast<int>(i);
    return -1;
}

inline int argMaxAbs(std::span<const float> x) {
    int best = 0;
    float bm = -1.0f;
    for (size_t i = 0; i < x.size(); ++i) {
        const float a = std::abs(x[i]);
        if (a > bm) { // strict: first index achieving the maximum
            bm = a;
            best = static_cast<int>(i);
        }
    }
    return best;
}

// ------------------------------------------------------------------ harness
/// Owns one engine; drives it in place over caller-provided stereo buffers.
struct Harness {
    ftc::FilterTableEngine engine;
    ftc::Parameters params{};
    ftc::FilterTableEngine::PrepareSpec spec{};

    /// setParameters BEFORE prepare so the initial synchronous kernel matches `params`.
    void prepare(double fs = kFs, int maxBlock = 512, int numChannels = 2) {
        spec.sampleRate = fs;
        spec.maxBlockSize = maxBlock;
        spec.numChannels = numChannels;
        engine.setParameters(params);
        engine.prepare(spec);
    }

    /// Process bufs[ch][start, start+n) in place with the current `params`.
    void block(std::vector<std::vector<float>>& bufs, size_t start, int n,
               const ftc::TransportInfo& t = {},
               std::span<const ftc::NoteEvent> notes = {}) {
        float* chs[8] = {};
        for (size_t c = 0; c < bufs.size() && c < 8; ++c)
            chs[c] = bufs[c].data() + start;
        engine.setParameters(params);
        engine.process(chs, static_cast<int>(bufs.size()), n, t, notes);
    }

    /// Render `input` duplicated to both channels with a fixed block size; returns channel 0.
    std::vector<float> renderMono(const std::vector<float>& input, int blockSize = 512) {
        std::vector<std::vector<float>> bufs{input, input};
        const int n = static_cast<int>(input.size());
        for (int p = 0; p < n; p += blockSize)
            block(bufs, static_cast<size_t>(p), std::min(blockSize, n - p));
        return bufs[0];
    }
};

/// residual = a - b (same length).
inline std::vector<float> residual(std::span<const float> a, std::span<const float> b) {
    std::vector<float> r(a.size());
    for (size_t i = 0; i < a.size(); ++i)
        r[i] = a[i] - b[i];
    return r;
}

/// Input delayed by `d` samples (zero fill).
inline std::vector<float> delayed(std::span<const float> x, int d) {
    std::vector<float> r(x.size(), 0.0f);
    for (size_t i = static_cast<size_t>(d); i < x.size(); ++i)
        r[i] = x[i - static_cast<size_t>(d)];
    return r;
}

} // namespace ftet
