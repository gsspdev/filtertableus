// FilterTableUS core — centralized engine tuning constants. FROZEN after Phase 0.
// Every size/cadence knob lives here so calibration is a one-file change.
#pragma once

namespace ftc {

struct EngineConfig {
    /// Modulators + smoothers are evaluated every controlInterval samples.
    static constexpr int controlInterval = 64;

    /// Direct-FIR head length of the zero-latency convolver (taps [0, headLength)).
    static constexpr int headLength = 128;
    /// Uniform tail partition length (FFT size = 2 * partitionLength).
    static constexpr int partitionLength = 128;

    /// FIR kernel length per sample-rate tier (constant ~43 ms support).
    static constexpr int kernelLength(double fs) noexcept {
        return fs <= 50000.0 ? 2048 : fs <= 100000.0 ? 4096 : 8192;
    }
    /// Kernel design/oversampling FFT size (bounds cepstral time-aliasing).
    static constexpr int designFftSize(double fs) noexcept { return 4 * kernelLength(fs); }
    /// Kernel rebuild + crossfade cadence (~2.67 ms, a multiple of controlInterval).
    static constexpr int kernelUpdateInterval(double fs) noexcept {
        return fs <= 50000.0 ? 128 : fs <= 100000.0 ? 256 : 512;
    }

    /// One-pole smoothing time constants (seconds).
    static constexpr double scanSmoothSeconds = 0.015;
    static constexpr double cutoffSmoothSeconds = 0.015;   // applied in log2(Hz) domain
    static constexpr double resonanceSmoothSeconds = 0.015;
    /// Per-sample linear ramp lengths (seconds).
    static constexpr double mixRampSeconds = 0.010;
    static constexpr double gainRampSeconds = 0.010;
    /// Wet fade time used to mask hard kernel swaps on phase-mode changes (seconds).
    static constexpr double modeSwitchFadeSeconds = 0.005;

    /// Magnitude floor used everywhere (log guards, response floors).
    static constexpr float floorDb = -120.0f;
    static constexpr float floorLinear = 1e-6f;

    /// Kernel-rebuild change-detection thresholds.
    static constexpr float scanDeltaThreshold = 1.0f / 4096.0f;
    static constexpr float cutoffLog2DeltaThreshold = 1.0f / 1200.0f; // ~1 cent
    static constexpr float resonanceDeltaThreshold = 1.0f / 1024.0f;

    /// The wavetable harmonic that sits exactly at the Cutoff frequency.
    static constexpr int cutoffHarmonic = 24;
    /// Cutoff clamp after modulation: [20 Hz, min(22 kHz, maxCutoffFsFraction * fs)].
    static constexpr double minCutoffHz = 20.0;
    static constexpr double maxCutoffHz = 22000.0;
    static constexpr double maxCutoffFsFraction = 0.45;
};

} // namespace ftc
