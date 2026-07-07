// FilterTableUS core — FIR kernel container + generation request. FROZEN after Phase 0.
#pragma once
#include <span>
#include "ftc/AlignedVector.h"
#include "ftc/Types.h"

namespace ftc {

/// Everything a kernel depends on. Values are post-modulation, post-smoothing.
struct KernelRequest {
    PhaseMode mode = PhaseMode::Minimum;
    float scan = 0.0f;        // 0..1
    float cutoffHz = 440.0f;  // clamped by the engine before the request
    float resonance = 0.0f;   // -1..+1
};

/// Preallocated tap container. prepare() allocates; everything else is RT-safe.
class Kernel {
public:
    void prepare(int maxLength) {
        taps_.assign(static_cast<size_t>(maxLength), 0.0f);
        maxLength_ = maxLength;
        length_ = maxLength;
        latency_ = 0;
    }
    float* data() noexcept { return taps_.data(); }
    const float* data() const noexcept { return taps_.data(); }
    std::span<const float> taps() const noexcept {
        return {taps_.data(), static_cast<size_t>(length_)};
    }
    void setLength(int len) noexcept { length_ = len <= maxLength_ ? len : maxLength_; }
    int length() const noexcept { return length_; }
    int maxLength() const noexcept { return maxLength_; }
    void setLatency(int samples) noexcept { latency_ = samples; }
    int latencySamples() const noexcept { return latency_; }

private:
    AlignedVector<float> taps_;
    int maxLength_ = 0;
    int length_ = 0;
    int latency_ = 0;
};

} // namespace ftc
