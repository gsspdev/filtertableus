// Shared utilities for the A1 convolver test suite (test_convolver*.cpp only).
#pragma once
#include <algorithm>
#include <cmath>
#include <random>
#include <span>
#include <vector>

#include "ftc/Convolver.h"
#include "ftc/Kernel.h"

namespace ftct {

inline std::vector<float> randomSignal(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v)
        x = dist(rng);
    return v;
}

/// Random dense kernel of length len (Kernel prepared at exactly len).
inline ftc::Kernel randomKernel(int len, unsigned seed, float scale = 1.0f) {
    ftc::Kernel k;
    k.prepare(len);
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    for (int i = 0; i < len; ++i)
        k.data()[i] = dist(rng) * scale;
    return k;
}

/// Kernel with a single unit tap at `at` (length len).
inline ftc::Kernel deltaKernel(int len, int at) {
    ftc::Kernel k;
    k.prepare(len);
    for (int i = 0; i < len; ++i)
        k.data()[i] = 0.0f;
    k.data()[at] = 1.0f;
    return k;
}

/// Scale taps to unit L2 norm (for click tests with bounded output level).
inline void normalizeKernel(ftc::Kernel& k) {
    double e = 0.0;
    for (int i = 0; i < k.length(); ++i)
        e += static_cast<double>(k.data()[i]) * static_cast<double>(k.data()[i]);
    const float g = e > 0.0 ? static_cast<float>(1.0 / std::sqrt(e)) : 1.0f;
    for (int i = 0; i < k.length(); ++i)
        k.data()[i] *= g;
}

/// Drive a mono convolver over x with a repeating block-size sequence; returns y (same length).
inline std::vector<float> runMono(ftc::PartitionedConvolver& c, const ftc::KernelImage& img,
                                  std::span<const float> x, std::span<const int> sizes) {
    std::vector<float> y(x.size(), 0.0f);
    size_t pos = 0, si = 0;
    while (pos < x.size()) {
        int n = sizes[si % sizes.size()];
        ++si;
        n = std::min<int>(n, static_cast<int>(x.size() - pos));
        c.process(img, x.data() + pos, y.data() + pos, n);
        pos += static_cast<size_t>(n);
    }
    return y;
}

inline float maxAbs(std::span<const float> v) {
    float m = 0.0f;
    for (float x : v)
        m = std::max(m, std::fabs(x));
    return m;
}

/// Max |a[i] - b[i]| over the common prefix (optionally a sub-range).
inline float maxAbsDiff(std::span<const float> a, std::span<const float> b, size_t begin = 0,
                        size_t end = SIZE_MAX) {
    const size_t n = std::min({a.size(), b.size(), end});
    float m = 0.0f;
    for (size_t i = begin; i < n; ++i)
        m = std::max(m, std::fabs(a[i] - b[i]));
    return m;
}

} // namespace ftct
