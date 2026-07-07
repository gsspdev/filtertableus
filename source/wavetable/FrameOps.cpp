#include "wavetable/FrameOps.h"

#include <algorithm>
#include <cmath>

namespace ftus::wtio {

void removeDc(std::span<float> frame) noexcept {
    if (frame.empty())
        return;
    double sum = 0.0;
    for (const float v : frame)
        sum += static_cast<double>(v);
    const float mean = static_cast<float>(sum / static_cast<double>(frame.size()));
    for (float& v : frame)
        v -= mean;
}

void normalizePeak(std::span<float> frame, float targetPeak) noexcept {
    float peak = 0.0f;
    for (const float v : frame)
        peak = std::max(peak, std::abs(v));
    if (peak < 1.0e-8f)
        return; // silence stays silence
    const float gain = targetPeak / peak;
    for (float& v : frame)
        v *= gain;
}

void wrapCrossfade(std::span<float> frame, int fadeLength) noexcept {
    const int n = static_cast<int>(frame.size());
    if (fadeLength < 2 || n < fadeLength * 2)
        return;
    const float denom = static_cast<float>(fadeLength - 1);
    for (int i = 0; i < fadeLength; ++i) {
        const float w = static_cast<float>(i) / denom; // 0 at fade start -> 1 at the seam
        const size_t tailIdx = static_cast<size_t>(n - fadeLength + i);
        frame[tailIdx] = (1.0f - w) * frame[tailIdx] + w * frame[static_cast<size_t>(i)];
    }
}

void postProcessFrame(std::span<float> frame) noexcept {
    removeDc(frame);
    normalizePeak(frame, kNormalizePeak);
    wrapCrossfade(frame, kWrapFadeLength);
}

namespace {

inline float catmullRom(float xm1, float x0, float x1, float x2, float t) noexcept {
    const float t2 = t * t;
    const float t3 = t2 * t;
    return 0.5f * ((2.0f * x0) + (-xm1 + x1) * t
                   + (2.0f * xm1 - 5.0f * x0 + 4.0f * x1 - x2) * t2
                   + (-xm1 + 3.0f * x0 - 3.0f * x1 + x2) * t3);
}

} // namespace

float catmullRomClamped(std::span<const float> src, double p) noexcept {
    const int n = static_cast<int>(src.size());
    if (n == 0)
        return 0.0f;
    if (n == 1)
        return src[0];
    const double fl = std::floor(p);
    int i = static_cast<int>(fl);
    float t = static_cast<float>(p - fl);
    auto at = [&](int idx) noexcept {
        return src[static_cast<size_t>(std::clamp(idx, 0, n - 1))];
    };
    return catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

float catmullRomCircular(std::span<const float> src, double p) noexcept {
    const int n = static_cast<int>(src.size());
    if (n == 0)
        return 0.0f;
    if (n == 1)
        return src[0];
    const double fl = std::floor(p);
    int i = static_cast<int>(fl);
    float t = static_cast<float>(p - fl);
    auto at = [&](int idx) noexcept {
        idx %= n;
        if (idx < 0)
            idx += n;
        return src[static_cast<size_t>(idx)];
    };
    return catmullRom(at(i - 1), at(i), at(i + 1), at(i + 2), t);
}

void resampleCycleCircular(std::span<const float> src, std::span<float> dst) noexcept {
    if (dst.empty())
        return;
    if (src.empty()) {
        std::fill(dst.begin(), dst.end(), 0.0f);
        return;
    }
    const double ratio = static_cast<double>(src.size()) / static_cast<double>(dst.size());
    for (size_t j = 0; j < dst.size(); ++j)
        dst[j] = catmullRomCircular(src, static_cast<double>(j) * ratio);
}

std::vector<int> evenStrideIndices(int total, int count) {
    std::vector<int> out;
    if (total <= 0 || count <= 0)
        return out;
    count = std::min(count, total);
    out.reserve(static_cast<size_t>(count));
    if (count == 1) {
        out.push_back(0);
        return out;
    }
    const double step = static_cast<double>(total - 1) / static_cast<double>(count - 1);
    for (int i = 0; i < count; ++i)
        out.push_back(static_cast<int>(std::lround(static_cast<double>(i) * step)));
    out.front() = 0;
    out.back() = total - 1;
    return out;
}

} // namespace ftus::wtio
