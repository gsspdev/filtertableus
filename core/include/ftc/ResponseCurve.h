// FilterTableUS core — decimated filter-response curve published for the GUI.
// FROZEN after Phase 0. The engine writes one of these into a TripleBuffer every kernel
// update; the GUI polls it on a timer. The log-frequency grid is defined HERE and ONLY here
// so the producer (engine) and consumer (SpectrumView) can never disagree.
#pragma once
#include <array>
#include <cmath>

namespace ftc {

struct ResponseCurve {
    static constexpr int kNumPoints = 256;
    static constexpr float kMinHz = 20.0f;
    static constexpr float kMaxHz = 20000.0f;

    /// Log-spaced grid: point 0 -> 20 Hz, point kNumPoints-1 -> 20 kHz.
    static float frequencyForPoint(int i) noexcept {
        const float t = static_cast<float>(i) / static_cast<float>(kNumPoints - 1);
        return kMinHz * std::pow(kMaxHz / kMinHz, t);
    }

    std::array<float, kNumPoints> db{}; // response magnitude in dB at each grid point
};

} // namespace ftc
