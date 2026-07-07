// FilterTableUS wavetable I/O — the 12 procedural factory tables (plan §4).
// Owned by the wavetable workstream. Implements ftus::generateFactoryTable declared in the
// frozen include/ftus/FactoryTables.h.
//
// Construction: every frame (except DigitalSteps, see below) is a sum of integer-multiple
// harmonic partials rendered as HALF-SAMPLE-SHIFTED cosines,
//     x[n] = sum_k c_k * cos(2*pi*k*(n + 0.5) / 2048),
// synthesized with one inverse FFT per frame. The half-sample shift makes each partial —
// and therefore the whole frame — exactly symmetric about the loop seam, so
// x[0] == x[2047] up to float rounding for ANY harmonic content, even dense high-harmonic
// clusters. Frames loop perfectly by construction; no wrap crossfade is applied (it would
// break that exact symmetry). DigitalSteps is built in the time domain with the mirror
// symmetry x[n] == x[2047-n], which gives the same exact-seam guarantee.
//
// Every frame is DC-removed and peak-normalized to 0.9. Deterministic: fixed per-table
// xorshift32 seeds, pure functions, no globals.
//
// Threading: allocates and runs FFTs; loader/message thread only (NOT the audio thread).
#include "ftus/FactoryTables.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <span>

#include "ftc/FFT.h"
#include "ftc/WavetableData.h"
#include "wavetable/FrameOps.h"

namespace ftus {

namespace {

constexpr int kN = ftc::WavetableData::kFrameLength; // 2048
constexpr int kBins = ftc::WavetableData::kNumBins;  // 1025
constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ deterministic rng ---
struct XorShift32 {
    uint32_t state;
    explicit XorShift32(uint32_t seed) : state(seed == 0 ? 0x9E3779B9u : seed) {}
    uint32_t next() {
        uint32_t x = state;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        state = x;
        return x;
    }
    /// uniform in [0, 1)
    double uniform() { return static_cast<double>(next() >> 8) * (1.0 / 16777216.0); }
    /// +1 or -1
    float sign() { return (next() & 1u) != 0 ? 1.0f : -1.0f; }
};

// --------------------------------------------------------------- additive frame render ---
/// Which way does a +0.5-sample delay rotate a bin's phase for THIS FFT implementation?
/// Probed once so the seam-symmetry guarantee cannot silently break if the underlying
/// FFT sign convention ever changes. Returns +1.0f or -1.0f for the imaginary part.
float halfShiftImagSign() {
    static const float sign = [] {
        ftc::RealFFT fft(kN);
        std::vector<std::complex<float>> spec(static_cast<size_t>(kBins), {0.0f, 0.0f});
        std::vector<float> time(static_cast<size_t>(kN), 0.0f);
        const int k = 3;
        const double phi = kPi * static_cast<double>(k) / static_cast<double>(kN);
        spec[static_cast<size_t>(k)] = {static_cast<float>(0.5 * kN * std::cos(phi)),
                                        static_cast<float>(0.5 * kN * std::sin(phi))};
        fft.inverse(spec.data(), time.data());
        return std::abs(time[0] - time[static_cast<size_t>(kN - 1)]) < 1.0e-3f ? 1.0f : -1.0f;
    }();
    return sign;
}

/// Render sum_k amps[k] * cos(2*pi*k*(n+0.5)/2048) into out (amps index k, [0] ignored,
/// k up to min(amps.size()-1, 1023)), then DC-remove + peak-normalize 0.9.
void renderAdditiveFrame(ftc::RealFFT& fft, std::span<const float> amps,
                         std::vector<std::complex<float>>& specScratch, float* out) {
    const float imagSign = halfShiftImagSign();
    std::fill(specScratch.begin(), specScratch.end(), std::complex<float>{0.0f, 0.0f});
    const int kMax = std::min(static_cast<int>(amps.size()) - 1, kN / 2 - 1);
    for (int k = 1; k <= kMax; ++k) {
        const float a = amps[static_cast<size_t>(k)];
        if (a == 0.0f)
            continue;
        const double phi = kPi * static_cast<double>(k) / static_cast<double>(kN);
        specScratch[static_cast<size_t>(k)] = {
            a * 0.5f * static_cast<float>(kN) * static_cast<float>(std::cos(phi)),
            a * 0.5f * static_cast<float>(kN) * static_cast<float>(std::sin(phi)) * imagSign};
    }
    fft.inverse(specScratch.data(), out);
    std::span<float> frame{out, static_cast<size_t>(kN)};
    wtio::removeDc(frame);
    wtio::normalizePeak(frame, wtio::kNormalizePeak);
}

/// Fixed per-table random sign pattern (constant across frames -> smooth morphs).
std::vector<float> seededSigns(uint32_t seed, int kMax) {
    XorShift32 rng(seed);
    std::vector<float> s(static_cast<size_t>(kMax) + 1, 1.0f);
    for (int k = 1; k <= kMax; ++k)
        s[static_cast<size_t>(k)] = rng.sign();
    return s;
}

/// Frame position u in [0, 1].
double framePos(int frame, int numFrames) {
    return numFrames > 1 ? static_cast<double>(frame) / static_cast<double>(numFrames - 1) : 0.0;
}

double cosEase(double t) { return 0.5 - 0.5 * std::cos(kPi * std::clamp(t, 0.0, 1.0)); }

/// Lorentzian resonance gain.
double resonanceGain(double f, double centre, double bw) {
    const double d = (f - centre) / bw;
    return 1.0 / (1.0 + d * d);
}

// --------------------------------------------------------------------- table builders ---
// Each builder fills amps[1..kMax] (signed harmonic coefficients) for frame position u.

// 1. AnalogMorph — sine -> triangle -> saw -> square, 64 frames.
void analogMorphAmps(double u, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    auto shapeCoef = [](int shape, int k) -> double {
        switch (shape) {
            case 0: return k == 1 ? 1.0 : 0.0;                                       // sine
            case 1: return (k % 2 == 1) ? (((k - 1) / 2) % 2 == 0 ? 1.0 : -1.0) /
                                              (static_cast<double>(k) * k)
                                        : 0.0;                                       // triangle
            case 2: return ((k % 2 == 1) ? 1.0 : -1.0) / static_cast<double>(k);     // saw
            default: return (k % 2 == 1) ? 1.0 / static_cast<double>(k) : 0.0;       // square
        }
    };
    const double pos = u * 3.0;
    const int seg = std::min(2, static_cast<int>(pos));
    const double t = cosEase(pos - seg);
    for (int k = 1; k <= kMax; ++k)
        amps[static_cast<size_t>(k)] = static_cast<float>(
            (1.0 - t) * shapeCoef(seg, k) + t * shapeCoef(seg + 1, k));
}

// 2. Pwm — pulse width 50% -> 3%, 128 frames.
void pwmAmps(double u, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double duty = 0.5 - 0.47 * u;
    for (int k = 1; k <= kMax; ++k)
        amps[static_cast<size_t>(k)] = static_cast<float>(
            (2.0 / (kPi * k)) * std::sin(kPi * k * duty));
}

// 3. VowelMorph — A-E-I-O-U formant filterbank over a saw, 128 frames.
void vowelMorphAmps(double u, const std::vector<float>& signs, std::vector<float>& amps) {
    static constexpr double vowelF[5][3] = {
        {730.0, 1090.0, 2440.0},  // A
        {530.0, 1840.0, 2480.0},  // E
        {390.0, 1990.0, 2550.0},  // I
        {570.0, 840.0, 2410.0},   // O
        {440.0, 1020.0, 2240.0},  // U
    };
    static constexpr double formantAmp[3] = {1.0, 0.6, 0.25};
    static constexpr double formantBw[3] = {90.0, 120.0, 160.0};
    constexpr double f0Ref = 65.4064; // C2: harmonic k sits at k*f0Ref Hz

    const double pos = u * 4.0;
    const int seg = std::min(3, static_cast<int>(pos));
    const double t = cosEase(pos - seg);

    const int kMax = static_cast<int>(amps.size()) - 1;
    for (int k = 1; k <= kMax; ++k) {
        const double f = k * f0Ref;
        double gain = 0.0;
        for (int r = 0; r < 3; ++r) {
            const double fc = vowelF[seg][r] * std::pow(vowelF[seg + 1][r] / vowelF[seg][r], t);
            gain += formantAmp[r] * resonanceGain(f, fc, formantBw[r]);
        }
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] *
                                       static_cast<float>((0.015 + gain) / std::sqrt(static_cast<double>(k)));
    }
}

// 4. CombSweep — comb spacing 24 -> 3 harmonics, 96 frames.
void combSweepAmps(double u, const std::vector<float>& signs, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double spacing = 24.0 * std::pow(3.0 / 24.0, u);
    for (int k = 1; k <= kMax; ++k) {
        const double comb = 0.5 * (1.0 + std::cos(2.0 * kPi * k / spacing));
        const double gain = 0.03 + 0.97 * comb * comb;
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] *
                                       static_cast<float>(gain / std::pow(static_cast<double>(k), 0.6));
    }
}

// 5. NotchArray — 5 spreading notches over a saw spectrum, 64 frames.
void notchArrayAmps(double u, const std::vector<float>& signs, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double spread = 6.0 + 22.0 * u;
    for (int k = 1; k <= kMax; ++k) {
        double gain = 1.0;
        for (int j = 1; j <= 5; ++j) {
            const double d = (k - j * spread) / 1.8;
            gain *= 1.0 - 0.97 * std::exp(-d * d);
        }
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] *
                                       static_cast<float>(gain / std::pow(static_cast<double>(k), 0.8));
    }
}

// 6. HarmonicLadder — 1 -> 32 partials, 128 frames.
void harmonicLadderAmps(double u, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double m = 1.0 + 31.0 * u;
    const int full = static_cast<int>(m);
    const double frac = m - full;
    for (int k = 1; k <= kMax; ++k) {
        double a = 0.0;
        if (k <= full)
            a = 1.0 / std::sqrt(static_cast<double>(k));
        else if (k == full + 1)
            a = frac / std::sqrt(static_cast<double>(k));
        amps[static_cast<size_t>(k)] = static_cast<float>(a);
    }
}

// 7. OddEvenMorph — odd-rich -> even-rich (fundamental anchored), 64 frames.
void oddEvenMorphAmps(double u, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    amps[1] = 1.0f;
    for (int k = 2; k <= kMax; ++k) {
        const double base = 1.0 / std::pow(static_cast<double>(k), 0.8);
        const double w = (k % 2 == 1) ? (1.0 - u) : 1.2 * u;
        amps[static_cast<size_t>(k)] = static_cast<float>(w * base);
    }
}

// 8. SpectralDrift — seeded smoothed random spectra drifting across 128 frames.
struct SpectralDriftKeys {
    std::array<std::vector<float>, 4> keys;
    explicit SpectralDriftKeys(uint32_t seed, int kMax) {
        XorShift32 rng(seed);
        for (auto& key : keys) {
            key.assign(static_cast<size_t>(kMax) + 1, 0.0f);
            for (int k = 1; k <= kMax; ++k)
                key[static_cast<size_t>(k)] = static_cast<float>(std::pow(rng.uniform(), 1.5));
            // two [0.25 0.5 0.25] smoothing passes over the harmonic axis
            for (int pass = 0; pass < 2; ++pass) {
                std::vector<float> src = key;
                for (int k = 1; k <= kMax; ++k) {
                    const float lo = src[static_cast<size_t>(std::max(1, k - 1))];
                    const float hi = src[static_cast<size_t>(std::min(kMax, k + 1))];
                    key[static_cast<size_t>(k)] = 0.25f * lo + 0.5f * src[static_cast<size_t>(k)] + 0.25f * hi;
                }
            }
            // gentle spectral tilt so it reads as a filter bank, not white fizz
            for (int k = 1; k <= kMax; ++k)
                key[static_cast<size_t>(k)] /= static_cast<float>(std::pow(static_cast<double>(k), 0.4));
        }
    }
};

void spectralDriftAmps(double u, const SpectralDriftKeys& keys, const std::vector<float>& signs,
                       std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double pos = u * 3.0;
    const int seg = std::min(2, static_cast<int>(pos));
    const double t = cosEase(pos - seg);
    const auto& a = keys.keys[static_cast<size_t>(seg)];
    const auto& b = keys.keys[static_cast<size_t>(seg + 1)];
    for (int k = 1; k <= kMax; ++k)
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] *
                                       static_cast<float>((1.0 - t) * a[static_cast<size_t>(k)] +
                                                          t * b[static_cast<size_t>(k)]);
}

// 9. FormantPeaks — two resonances sweeping in opposite directions (crossing), 64 frames.
void formantPeaksAmps(double u, const std::vector<float>& signs, std::vector<float>& amps) {
    constexpr double f0Ref = 65.4064;
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double f1 = 250.0 * std::pow(1500.0 / 250.0, u);  // up
    const double f2 = 3200.0 * std::pow(700.0 / 3200.0, u); // down
    const double bw1 = 0.12 * f1;
    const double bw2 = 0.15 * f2;
    for (int k = 1; k <= kMax; ++k) {
        const double f = k * f0Ref;
        const double gain = resonanceGain(f, f1, bw1) + 0.8 * resonanceGain(f, f2, bw2);
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] *
                                       static_cast<float>((0.02 + gain) /
                                                          std::pow(static_cast<double>(k), 0.4));
    }
}

// 10. DigitalSteps — mirrored staircase, 32 -> 2 levels, 64 frames (time-domain build;
//     x[n] == x[2047-n] keeps the seam exact).
void digitalStepsFrame(double u, float* out) {
    const int levels = std::max(2, static_cast<int>(std::lround(32.0 * std::pow(2.0 / 32.0, u))));
    const int half = kN / 2;
    for (int n = 0; n < half; ++n) {
        const double r = static_cast<double>(n) / static_cast<double>(half - 1); // 0..1
        const int q = std::min(levels - 1, static_cast<int>(r * levels));
        const float v = static_cast<float>(2.0 * (static_cast<double>(q) / (levels - 1)) - 1.0);
        out[n] = v;
        out[kN - 1 - n] = v;
    }
    std::span<float> frame{out, static_cast<size_t>(kN)};
    wtio::removeDc(frame);
    wtio::normalizePeak(frame, wtio::kNormalizePeak);
}

// 11. MetalCluster — four high-harmonic clusters sweeping upward, 64 frames.
void metalClusterAmps(double u, const std::vector<float>& signs, std::vector<float>& amps) {
    static constexpr double centre[4] = {21.0, 55.0, 144.0, 340.0};
    static constexpr double octaves[4] = {0.9, 0.7, 0.55, 0.45};
    static constexpr double clusterAmp[4] = {1.0, 0.85, 0.7, 0.55};
    constexpr double sigma = 0.055; // log2 width
    const int kMax = static_cast<int>(amps.size()) - 1;
    for (int k = 1; k <= kMax; ++k) {
        double a = 0.04 / std::pow(static_cast<double>(k), 1.2); // faint body
        for (int j = 0; j < 4; ++j) {
            const double c = std::min(static_cast<double>(kMax) - 8.0,
                                      centre[j] * std::pow(2.0, u * octaves[j]));
            const double d = std::log2(static_cast<double>(k) / c) / sigma;
            a += clusterAmp[j] * std::exp(-d * d);
        }
        if (k == 1)
            a += 0.18; // anchor so the fundamental never disappears
        amps[static_cast<size_t>(k)] = signs[static_cast<size_t>(k)] * static_cast<float>(a);
    }
}

// 12. SubBloom — low-pass rolloff opening from the fundamental, 64 frames.
void subBloomAmps(double u, std::vector<float>& amps) {
    const int kMax = static_cast<int>(amps.size()) - 1;
    const double kc = std::pow(64.0, u); // 1 -> 64
    for (int k = 1; k <= kMax; ++k) {
        const double lp = 1.0 / (1.0 + std::pow(static_cast<double>(k) / kc, 8.0));
        const double dres = (static_cast<double>(k) - kc) / std::max(1.0, 0.18 * kc);
        const double res = 1.0 + 1.2 * std::exp(-dres * dres);
        amps[static_cast<size_t>(k)] =
            static_cast<float>(lp * res / std::pow(static_cast<double>(k), 0.8));
    }
}

} // namespace

// ------------------------------------------------------------------------- entry point ---
RawTable generateFactoryTable(FactoryTableId id) {
    struct Config {
        int frames;
        int kMax;
        uint32_t seed; // 0 = analytic signs
    };
    auto configFor = [](FactoryTableId table) -> Config {
        switch (table) {
            case FactoryTableId::AnalogMorph:    return {64, 64, 0};
            case FactoryTableId::Pwm:            return {128, 128, 0};
            case FactoryTableId::VowelMorph:     return {128, 200, 0x00A11CEu};
            case FactoryTableId::CombSweep:      return {96, 192, 0x0C0FFEEu};
            case FactoryTableId::NotchArray:     return {64, 160, 0x0BADF00u};
            case FactoryTableId::HarmonicLadder: return {128, 32, 0};
            case FactoryTableId::OddEvenMorph:   return {64, 64, 0};
            case FactoryTableId::SpectralDrift:  return {128, 96, 0x5EED1E5u};
            case FactoryTableId::FormantPeaks:   return {64, 180, 0x0F0CA15u};
            case FactoryTableId::DigitalSteps:   return {64, 0, 0};
            case FactoryTableId::MetalCluster:   return {64, 512, 0x3E7A1C1u};
            case FactoryTableId::SubBloom:       return {64, 64, 0};
            default:                             return {64, 64, 0};
        }
    };

    const FactoryTableId safeId =
        (static_cast<int>(id) >= 0 && static_cast<int>(id) < kNumFactoryTables)
            ? id
            : FactoryTableId::AnalogMorph;
    const Config cfg = configFor(safeId);

    RawTable table;
    table.numFrames = cfg.frames;
    table.name = factoryTableDisplayName(safeId);
    table.samples.assign(static_cast<size_t>(cfg.frames) * kN, 0.0f);

    // DigitalSteps is built directly in the time domain.
    if (safeId == FactoryTableId::DigitalSteps) {
        for (int f = 0; f < cfg.frames; ++f)
            digitalStepsFrame(framePos(f, cfg.frames),
                              table.samples.data() + static_cast<size_t>(f) * kN);
        return table;
    }

    ftc::RealFFT fft(kN);
    std::vector<std::complex<float>> spec(static_cast<size_t>(kBins));
    std::vector<float> amps(static_cast<size_t>(cfg.kMax) + 1, 0.0f);
    const std::vector<float> signs =
        cfg.seed != 0 ? seededSigns(cfg.seed, cfg.kMax)
                      : std::vector<float>(static_cast<size_t>(cfg.kMax) + 1, 1.0f);

    // SpectralDrift precomputes its seeded keyframe spectra once.
    const SpectralDriftKeys driftKeys(0xD41F7u, cfg.kMax);

    for (int f = 0; f < cfg.frames; ++f) {
        const double u = framePos(f, cfg.frames);
        switch (safeId) {
            case FactoryTableId::AnalogMorph:    analogMorphAmps(u, amps); break;
            case FactoryTableId::Pwm:            pwmAmps(u, amps); break;
            case FactoryTableId::VowelMorph:     vowelMorphAmps(u, signs, amps); break;
            case FactoryTableId::CombSweep:      combSweepAmps(u, signs, amps); break;
            case FactoryTableId::NotchArray:     notchArrayAmps(u, signs, amps); break;
            case FactoryTableId::HarmonicLadder: harmonicLadderAmps(u, amps); break;
            case FactoryTableId::OddEvenMorph:   oddEvenMorphAmps(u, amps); break;
            case FactoryTableId::SpectralDrift:  spectralDriftAmps(u, driftKeys, signs, amps); break;
            case FactoryTableId::FormantPeaks:   formantPeaksAmps(u, signs, amps); break;
            case FactoryTableId::MetalCluster:   metalClusterAmps(u, signs, amps); break;
            case FactoryTableId::SubBloom:       subBloomAmps(u, amps); break;
            default:                             analogMorphAmps(u, amps); break;
        }
        renderAdditiveFrame(fft, amps, spec,
                            table.samples.data() + static_cast<size_t>(f) * kN);
    }
    return table;
}

} // namespace ftus
