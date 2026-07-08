// FilterTableUS core — ftc::KernelGenerator implementation (workstream A2).
//
// Pipeline (docs/PLAN.md §2 "Response construction" / "Kernel reconstruction",
// docs/briefs/brief-a2-kernelgen.md):
//
//   Spectral path (Minimum, Linear):
//     scan-lerp the two adjacent frames' linear magnitudes (1025 harmonic bins)
//       -> resonance contrast about the in-band mean (dB domain, floored)
//       -> harmonic-24 cutoff mapping onto the N = 4L design grid
//       -> peak normalize to 0 dB
//       -> Minimum: real-cepstrum minimum phase, truncate L, half-Hann fade last L/8 (lat 0)
//          Linear:  magnitude * linear-phase ramp, inverse FFT, Tukey(0.25) window,
//                   exact tap symmetry (lat L/2)
//       -> REALIZED-peak normalize to 0 dB (|FFT| of the truncated/windowed kernel — design
//          normalization alone lets truncation/window ripple overshoot ~+1 dB at low fc)
//
//   Cyclic path (Original, Raw):
//     scan-lerp the two frames' complex spectra
//       -> resonance applied to |S| with phases untouched (same applyResonance)
//       -> Original: zero harmonics mapping above Nyquist (k*fc/24 > fs/2)
//       -> inverse 2048-pt FFT = one cycle
//       -> circular Catmull-Rom resample so one cycle spans P0 = 24*fs/fc output samples
//       -> Original: anchored at tap L/2 (constant latency L/2), Tukey window,
//                    response-peak normalize to 0 dB
//          Raw:      causal from tap 0, no window, hard truncate, energy Sum h^2 = 1 (lat 0)
//
// Calibration switches (compile-time, one site each — see docs/PLAN.md §9 risk register):
//   FTUS_SCAN_LERP_DB              0 = linear-magnitude scan morph (default); 1 = dB-domain
//   FTUS_LOW_END_POLICY            0 = LowEndPolicy::InterpToDC (default); 1 = HoldH1
//   FTUS_LINEAR_HALF_SAMPLE_CENTER 0 = integer center at tap L/2 (DEFAULT since Wave-3):
//                                      symmetric about tap L/2 (taps[i]==taps[L-i], i>=1),
//                                      group delay exactly the reported L/2, so LINEAR mode
//                                      nulls broadband against the latency-aligned dry path
//                                      at 50% mix (the product-defining behavior; the engine
//                                      null suite asserts the strict -60 dBFS null).
//                                      1 = even-tap symmetry taps[i]==taps[L-1-i], group delay
//                                      (L-1)/2 (A2's original acceptance): a half-sample HF
//                                      comb vs dry (-4 dB @10 kHz/48 k) that no listening
//                                      upside justifies. Kept selectable for calibration A/B
//                                      (docs/CALIBRATION.md).
//
// RT contract: prepare() allocates; setWavetable()/generate() never allocate, lock or throw.

#include "ftc/KernelGenerator.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <optional>

#include "ftc/EngineConfig.h"
#include "ftc/FFT.h"
#include "ftc/FastMath.h"
#include "ftc/WavetableData.h"

#ifndef FTUS_SCAN_LERP_DB
#define FTUS_SCAN_LERP_DB 0
#endif
#ifndef FTUS_LOW_END_POLICY
#define FTUS_LOW_END_POLICY 0
#endif
#ifndef FTUS_LINEAR_HALF_SAMPLE_CENTER
#define FTUS_LINEAR_HALF_SAMPLE_CENTER 0
#endif

namespace ftc {

namespace {

enum class LowEndPolicy : int { InterpToDC = 0, HoldH1 = 1 };
constexpr LowEndPolicy kLowEndPolicy = static_cast<LowEndPolicy>(FTUS_LOW_END_POLICY);

constexpr double kTwoPi = 6.283185307179586476925286766559;
constexpr double kPi = 3.1415926535897932384626433832795;

constexpr int kBins = WavetableData::kNumBins;         // 1025 (DC .. harmonic 1024)
constexpr int kFrameLen = WavetableData::kFrameLength; // 2048
constexpr int kFrameMask = kFrameLen - 1;
constexpr int kTopHarmonic = kBins - 1;                // 1024

/// Half-cosine taper width (in harmonics) below the h = 1024 edge — kills brickwall ringing.
constexpr double kEdgeTaperHarmonics = 8.0;

/// -120 dB expressed in natural-log units: (120 / 20) * ln(10).
constexpr float kLnFloorRange = 13.815510558f;

/// Peaks below this are treated as silence: leave the response floored instead of
/// normalizing numerical noise up to 0 dB (frame magnitudes are ~1024 at full scale).
constexpr float kSilencePeak = 1e-5f;

/// Clamp on Re(lnH) before the complex exp of the minimum-phase log-spectrum.
constexpr float kMinPhaseLnClamp = 10.0f;

/// Circular Catmull-Rom read of one 2048-sample cycle at fractional position pos in [0, 2048).
inline float catmullRomCircular(const float* c, double pos) noexcept {
    const int j = static_cast<int>(pos);
    const float x = static_cast<float>(pos - static_cast<double>(j));
    const float p0 = c[(j - 1) & kFrameMask];
    const float p1 = c[j & kFrameMask];
    const float p2 = c[(j + 1) & kFrameMask];
    const float p3 = c[(j + 2) & kFrameMask];
    const float a3 = -p0 + 3.0f * p1 - 3.0f * p2 + p3;
    const float a2 = 2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3;
    const float a1 = p2 - p0;
    return 0.5f * (2.0f * p1 + x * (a1 + x * (a2 + x * a3)));
}

} // namespace

struct KernelGenerator::Impl {
    Config cfg{};
    bool prepared = false;
    const WavetableData* table = nullptr;

    int L = 0;    // kernel taps
    int N = 0;    // design FFT size = 4L
    int half = 0; // N/2 (bins = half + 1)

    std::optional<RealFFT> designFft; // N-point
    std::optional<RealFFT> cyclicFft; // 2048-point

    // Harmonic-domain scratch (1025 bins).
    AlignedVector<float> mags;                // lerped / resonated magnitudes
    AlignedVector<float> magsPre;             // pre-resonance magnitudes (cyclic gain ref)
    AlignedVector<float> lnScratchA;          // ln-domain scratch
    AlignedVector<float> lnScratchB;
    AlignedVector<std::complex<float>> spec;  // lerped complex spectrum (cyclic path)

    // Design-grid scratch (half + 1 bins / N samples).
    AlignedVector<float> designMag;
    AlignedVector<float> lnDesign;
    AlignedVector<std::complex<float>> designSpec;
    AlignedVector<std::complex<float>> linearRamp; // e^{-j 2 pi k d / N}, d = linear-phase delay
    AlignedVector<float> timeA;
    AlignedVector<float> timeB;

    AlignedVector<float> cycle;   // one 2048-sample cyclic period
    AlignedVector<float> tukey;   // L-tap symmetric Tukey(0.25) window
    AlignedVector<float> minFade; // half-Hann fade for the last L/8 Minimum taps

    // ---------------------------------------------------------------- prepare
    void prepare(const Config& c) {
        cfg = c;
        L = c.kernelLength;
        N = 4 * L;
        half = N / 2;

        designFft.emplace(N);
        cyclicFft.emplace(kFrameLen);

        mags.assign(static_cast<size_t>(kBins), 0.0f);
        magsPre.assign(static_cast<size_t>(kBins), 0.0f);
        lnScratchA.assign(static_cast<size_t>(kBins), 0.0f);
        lnScratchB.assign(static_cast<size_t>(kBins), 0.0f);
        spec.assign(static_cast<size_t>(kBins), std::complex<float>{});

        designMag.assign(static_cast<size_t>(half + 1), 0.0f);
        lnDesign.assign(static_cast<size_t>(half + 1), 0.0f);
        designSpec.assign(static_cast<size_t>(half + 1), std::complex<float>{});
        linearRamp.assign(static_cast<size_t>(half + 1), std::complex<float>{});
        timeA.assign(static_cast<size_t>(N), 0.0f);
        timeB.assign(static_cast<size_t>(N), 0.0f);
        cycle.assign(static_cast<size_t>(kFrameLen), 0.0f);

        // Linear-phase delay (see FTUS_LINEAR_HALF_SAMPLE_CENTER above).
#if FTUS_LINEAR_HALF_SAMPLE_CENTER
        const double d = (static_cast<double>(L) - 1.0) * 0.5;
#else
        const double d = static_cast<double>(L) * 0.5;
#endif
        for (int k = 0; k <= half; ++k) {
            const double ph = -kTwoPi * static_cast<double>(k) * d / static_cast<double>(N);
            linearRamp[static_cast<size_t>(k)] = {static_cast<float>(std::cos(ph)),
                                                  static_cast<float>(std::sin(ph))};
        }

        // Symmetric Tukey(0.25): cosine taper over L/8 samples each side, exact pair symmetry
        // about the compiled linear-phase center (matching the kernel's symmetry so windowing
        // preserves it exactly).
        tukey.assign(static_cast<size_t>(L), 1.0f);
        const int taper = L / 8;
        for (int i = 0; i < taper; ++i) {
            const double x = (static_cast<double>(i) + 1.0) / (static_cast<double>(taper) + 1.0);
            const float w = static_cast<float>(0.5 * (1.0 - std::cos(kPi * x)));
#if FTUS_LINEAR_HALF_SAMPLE_CENTER
            tukey[static_cast<size_t>(i)] = w;         // pairs (i, L-1-i): center (L-1)/2
            tukey[static_cast<size_t>(L - 1 - i)] = w;
#else
            tukey[static_cast<size_t>(1 + i)] = w;     // pairs (1+i, L-1-i): center L/2
            tukey[static_cast<size_t>(L - 1 - i)] = w;
#endif
        }
#if !FTUS_LINEAR_HALF_SAMPLE_CENTER
        tukey[0] = 0.0f; // unpaired edge tap sits outside the (L-1)-tap symmetric support
#endif

        // Half-Hann fade-out over the last L/8 taps of the Minimum kernel.
        minFade.assign(static_cast<size_t>(taper), 0.0f);
        for (int j = 0; j < taper; ++j)
            minFade[static_cast<size_t>(j)] = static_cast<float>(
                0.5 * (1.0 + std::cos(kPi * (static_cast<double>(j) + 1.0)
                                      / static_cast<double>(taper))));

        prepared = true;
    }

    // --------------------------------------------------------------- generate
    void generate(const KernelRequest& req, Kernel& out) noexcept {
        if (!prepared || table == nullptr || out.maxLength() < L) {
            writePassthrough(req.mode, out);
            return;
        }

        const float scan = std::clamp(req.scan, 0.0f, 1.0f);
        const float res = std::clamp(req.resonance, -1.0f, 1.0f);
        const double fc = std::clamp(static_cast<double>(req.cutoffHz), 0.01, cfg.sampleRate);

        const int numFrames = table->numFrames();
        const float framePos = scan * static_cast<float>(numFrames - 1);
        int f0 = static_cast<int>(framePos);
        if (f0 > numFrames - 1)
            f0 = numFrames - 1;
        const int f1 = std::min(f0 + 1, numFrames - 1);
        const float t = framePos - static_cast<float>(f0);

        switch (req.mode) {
            case PhaseMode::Minimum:
            case PhaseMode::Linear:
                spectralPath(req.mode, f0, f1, t, fc, res, out);
                break;
            case PhaseMode::Original:
            case PhaseMode::Raw:
                cyclicPath(req.mode, f0, f1, t, fc, res, out);
                break;
        }
    }

    // Unit impulse (latency-0 modes) / centered impulse (L/2 modes) when there is no table.
    void writePassthrough(PhaseMode mode, Kernel& out) noexcept {
        const int len = std::min(L > 0 ? L : out.maxLength(), out.maxLength());
        if (len <= 0) {
            out.setLength(0);
            out.setLatency(0);
            return;
        }
        float* tp = out.data();
        std::memset(tp, 0, sizeof(float) * static_cast<size_t>(len));
        const int lat = (mode == PhaseMode::Linear || mode == PhaseMode::Original) ? len / 2 : 0;
        tp[lat] = 1.0f;
        out.setLength(len);
        out.setLatency(lat);
    }

    // -------------------------------------------------------------- scan lerp
    /// THE scan-morph site for the spectral path (risk register). Linear-magnitude lerp by
    /// default; FTUS_SCAN_LERP_DB=1 morphs in the dB (geometric) domain instead.
    inline void lerpMagnitudeArrays(const float* a, const float* b, float t,
                                    float* outArr) noexcept {
        if (t <= 0.0f) {
            std::memcpy(outArr, a, sizeof(float) * static_cast<size_t>(kBins));
            return;
        }
#if FTUS_SCAN_LERP_DB
        logAbsApprox(a, lnScratchA.data(), kBins);
        logAbsApprox(b, lnScratchB.data(), kBins);
        for (int i = 0; i < kBins; ++i)
            lnScratchA[static_cast<size_t>(i)] +=
                t * (lnScratchB[static_cast<size_t>(i)] - lnScratchA[static_cast<size_t>(i)]);
        expApprox(lnScratchA.data(), outArr, kBins);
#else
        for (int i = 0; i < kBins; ++i)
            outArr[i] = a[i] + t * (b[i] - a[i]);
#endif
    }

    // -------------------------------------------------------------- resonance
    /// Resonance contrast (risk register: highest-risk calibration item — keep isolated).
    /// r in [-1, +1] -> k_r = 4^r. In log domain about the in-band mean (harmonics 1..1024):
    ///   ln' = mean + k_r * (ln - mean), floored at (max' - 120 dB).
    /// Operates in natural log (identical to dB up to a constant factor) so the FastMath
    /// vector helpers convert whole arrays.
    void applyResonance(float* m, float r) noexcept {
        logAbsApprox(m, lnScratchA.data(), kBins);
        float* ln = lnScratchA.data();

        float mx = ln[1];
        for (int k = 2; k < kBins; ++k)
            mx = std::max(mx, ln[k]);
        const float preFloor = mx - kLnFloorRange;

        double sum = 0.0;
        for (int k = 1; k < kBins; ++k) {
            const float v = std::max(ln[k], preFloor);
            ln[k] = v;
            sum += static_cast<double>(v);
        }
        ln[0] = std::max(ln[0], preFloor);
        const float mean = static_cast<float>(sum / static_cast<double>(kBins - 1));

        const float kr = std::exp2(2.0f * r); // 4^r
        const float newMax = mean + kr * (mx - mean);
        const float postFloor = newMax - kLnFloorRange;
        for (int k = 0; k < kBins; ++k)
            ln[k] = std::max(mean + kr * (ln[k] - mean), postFloor);

        expApprox(ln, m, kBins); // expApprox clamps its input to [-87, +87]
    }

    // ---------------------------------------------------------- spectral path
    void spectralPath(PhaseMode mode, int f0, int f1, float t, double fc, float res,
                      Kernel& out) noexcept {
        lerpMagnitudeArrays(table->magnitudes(f0).data(), table->magnitudes(f1).data(), t,
                            mags.data());
        applyResonance(mags.data(), res);
        mapToDesignGrid(fc);
        normalizeDesignPeak();
        if (mode == PhaseMode::Minimum)
            minimumPhase(out);
        else
            linearPhase(out);
    }

    /// Harmonic-24 cutoff mapping: design bin k at f_k = k*fs/N reads continuous harmonic
    /// index h = 24*f_k/fc from the 1025-bin magnitude array.
    void mapToDesignGrid(double fc) noexcept {
        const double hStep = 24.0 * cfg.sampleRate / (static_cast<double>(N) * fc);
        const double taperStart = static_cast<double>(kTopHarmonic) - kEdgeTaperHarmonics;
        const float* m = mags.data();
        float* dst = designMag.data();
        const float dc = m[0];
        int k = 0;
        for (; k <= half; ++k) {
            const double h = static_cast<double>(k) * hStep;
            if (h > static_cast<double>(kTopHarmonic))
                break;
            float v;
            if (h >= 1.0) {
                const int j = static_cast<int>(h);
                const float frac = static_cast<float>(h - static_cast<double>(j));
                const int j1 = j < kTopHarmonic ? j + 1 : kTopHarmonic;
                v = m[j] + frac * (m[j1] - m[j]);
            } else if constexpr (kLowEndPolicy == LowEndPolicy::InterpToDC) {
                v = dc + static_cast<float>(h) * (m[1] - dc);
            } else {
                v = m[1];
            }
            if (h > taperStart) {
                const double x = (h - taperStart) / kEdgeTaperHarmonics; // 0..1
                v *= static_cast<float>(0.5 * (1.0 + std::cos(kPi * x)));
            }
            dst[k] = v;
        }
        if (k <= half)
            std::memset(dst + k, 0, sizeof(float) * static_cast<size_t>(half + 1 - k));
    }

    /// Normalize the design magnitude so the response peak sits at 0 dB (the filter only
    /// attenuates; resonance reads as "everything else ducks"). Guarded so silence stays
    /// floored instead of being amplified to unity.
    void normalizeDesignPeak() noexcept {
        float peak = 0.0f;
        for (int k = 0; k <= half; ++k)
            peak = std::max(peak, designMag[static_cast<size_t>(k)]);
        if (peak > kSilencePeak) {
            const float s = 1.0f / peak;
            for (int k = 0; k <= half; ++k)
                designMag[static_cast<size_t>(k)] *= s;
        }
    }

    /// Real-cepstrum minimum phase: ln M -> inverse FFT -> causal fold -> forward FFT ->
    /// complex exp -> inverse FFT -> truncate to L with a half-Hann fade on the last L/8.
    void minimumPhase(Kernel& out) noexcept {
        // Post-normalization peak is 1, so logAbsApprox's absolute 1e-6 clamp IS the
        // (max - 120 dB) floor required by the design.
        logAbsApprox(designMag.data(), lnDesign.data(), half + 1);
        for (int k = 0; k <= half; ++k)
            designSpec[static_cast<size_t>(k)] = {lnDesign[static_cast<size_t>(k)], 0.0f};
        designFft->inverse(designSpec.data(), timeA.data()); // real cepstrum (1/N scaled)

        float* fold = timeB.data();
        const float* c = timeA.data();
        fold[0] = c[0];
        for (int n = 1; n < half; ++n)
            fold[n] = 2.0f * c[n];
        fold[half] = c[half];
        std::memset(fold + half + 1, 0, sizeof(float) * static_cast<size_t>(N - half - 1));

        designFft->forward(fold, designSpec.data()); // lnH = ln M + j * (minimum phase)
        for (int k = 0; k <= half; ++k) {
            if (designSpec[static_cast<size_t>(k)].real() > kMinPhaseLnClamp)
                designSpec[static_cast<size_t>(k)].real(kMinPhaseLnClamp);
        }
        expComplex(designSpec.data(), designSpec.data(), half + 1);
        designFft->inverse(designSpec.data(), timeA.data()); // h[n], 1/N scaled

        float* tp = out.data();
        std::memcpy(tp, timeA.data(), sizeof(float) * static_cast<size_t>(L));
        const int fadeLen = static_cast<int>(minFade.size());
        for (int j = 0; j < fadeLen; ++j)
            tp[L - fadeLen + j] *= minFade[static_cast<size_t>(j)];
        // Realized-peak normalization: truncation + fade ripple can push the realized
        // response up to ~+1 dB past the 0 dB design peak at low fc. Same treatment as
        // Original mode; a uniform scale preserves minimum phase. Allocation-free (prepared
        // timeA/designSpec scratch).
        normalizeKernelPeak(tp);
        out.setLength(L);
        out.setLatency(0);
    }

    /// Zero-phase magnitude with a precomputed linear-phase ramp -> inverse FFT -> Tukey
    /// window -> exact tap symmetrization (default center L/2; see the switch table above).
    void linearPhase(Kernel& out) noexcept {
        for (int k = 0; k <= half; ++k)
            designSpec[static_cast<size_t>(k)] =
                designMag[static_cast<size_t>(k)] * linearRamp[static_cast<size_t>(k)];
        designFft->inverse(designSpec.data(), timeA.data());

        float* tp = out.data();
        const float* src = timeA.data();
        const float* w = tukey.data();
        for (int i = 0; i < L; ++i)
            tp[i] = src[i] * w[i];
#if FTUS_LINEAR_HALF_SAMPLE_CENTER
        // Project onto the exactly symmetric FIR about (L-1)/2 (removes FFT rounding skew).
        for (int i = 0, j = L - 1; i < j; ++i, --j) {
            const float avg = 0.5f * (tp[i] + tp[j]);
            tp[i] = avg;
            tp[j] = avg;
        }
#else
        // Project onto the exactly symmetric FIR about tap L/2 (removes FFT rounding skew;
        // tap 0 is the unpaired edge, already zeroed by the window).
        tp[0] = 0.0f;
        for (int i = 1, j = L - 1; i < j; ++i, --j) {
            const float avg = 0.5f * (tp[i] + tp[j]);
            tp[i] = avg;
            tp[j] = avg;
        }
#endif
        // Realized-peak normalization (see minimumPhase): window ripple can overshoot 0 dB
        // at low fc. A uniform scale preserves the exact tap symmetry established above.
        normalizeKernelPeak(tp);
        out.setLength(L);
        out.setLatency(L / 2);
    }

    // ------------------------------------------------------------ cyclic path
    void cyclicPath(PhaseMode mode, int f0, int f1, float t, double fc, float res,
                    Kernel& out) noexcept {
        const std::complex<float>* sa = table->spectrum(f0).data();
        const std::complex<float>* sb = table->spectrum(f1).data();
        std::complex<float>* s = spec.data();
        if (t <= 0.0f)
            std::memcpy(s, sa, sizeof(std::complex<float>) * static_cast<size_t>(kBins));
        else
            for (int k = 0; k < kBins; ++k)
                s[k] = sa[k] + t * (sb[k] - sa[k]);

        // Resonance on |S| with phases untouched (same applyResonance as the spectral path).
        for (int k = 0; k < kBins; ++k)
            magsPre[static_cast<size_t>(k)] = std::abs(s[k]);
        std::memcpy(mags.data(), magsPre.data(), sizeof(float) * static_cast<size_t>(kBins));
        applyResonance(mags.data(), res);
        for (int k = 0; k < kBins; ++k) {
            const float pre = magsPre[static_cast<size_t>(k)];
            // Near-zero bins keep zero instead of being floor-raised: their phase is
            // numerical noise and must not be amplified. No upper cap: |S'| equals the
            // transformed magnitude, which the floor range bounds to ~e^51 (finite; the
            // later peak/energy normalization rescales).
            const float g = pre > 1e-9f ? mags[static_cast<size_t>(k)] / pre : 0.0f;
            s[k] *= g;
        }

        if (mode == PhaseMode::Original) {
            // Band-limit: zero harmonics whose mapped frequency k*fc/24 exceeds fs/2.
            const double kMaxD = 12.0 * cfg.sampleRate / fc;
            const int kMax = kMaxD >= static_cast<double>(kTopHarmonic)
                                 ? kTopHarmonic
                                 : static_cast<int>(kMaxD);
            for (int k = kMax + 1; k < kBins; ++k)
                s[k] = {0.0f, 0.0f};
        }

        cyclicFft->inverse(s, cycle.data()); // one 2048-sample cycle (1/2048 scaled)

        // One cycle spans P0 = 24*fs/fc output samples -> read the 2048-sample cycle
        // circularly with step 2048/P0.
        const double rate = static_cast<double>(kFrameLen) * fc / (24.0 * cfg.sampleRate);
        float* tp = out.data();

        if (mode == PhaseMode::Original) {
            // Anchor cycle position 0 at tap L/2: constant latency L/2, never varies with fc.
            double pos =
                std::fmod(-static_cast<double>(L / 2) * rate, static_cast<double>(kFrameLen));
            if (pos < 0.0)
                pos += static_cast<double>(kFrameLen);
            const float* w = tukey.data();
            for (int i = 0; i < L; ++i) {
                tp[i] = catmullRomCircular(cycle.data(), pos) * w[i];
                pos += rate;
                while (pos >= static_cast<double>(kFrameLen))
                    pos -= static_cast<double>(kFrameLen);
            }
            normalizeKernelPeak(tp);
            out.setLength(L);
            out.setLatency(L / 2);
        } else { // Raw: causal from tap 0, no band-limit, no window, hard truncate at L.
            double pos = 0.0;
            for (int i = 0; i < L; ++i) {
                tp[i] = catmullRomCircular(cycle.data(), pos);
                pos += rate;
                while (pos >= static_cast<double>(kFrameLen))
                    pos -= static_cast<double>(kFrameLen);
            }
            double energy = 0.0;
            for (int i = 0; i < L; ++i)
                energy += static_cast<double>(tp[i]) * static_cast<double>(tp[i]);
            if (energy > 1e-20) {
                const float sNorm = static_cast<float>(1.0 / std::sqrt(energy));
                for (int i = 0; i < L; ++i)
                    tp[i] *= sNorm;
            }
            out.setLength(L);
            out.setLatency(0);
        }
    }

    /// Measure the kernel's own response peak on the design grid and scale it to 0 dB
    /// (Minimum, Linear and Original modes; guarded so silence is not amplified).
    void normalizeKernelPeak(float* tp) noexcept {
        std::memcpy(timeA.data(), tp, sizeof(float) * static_cast<size_t>(L));
        std::memset(timeA.data() + L, 0, sizeof(float) * static_cast<size_t>(N - L));
        designFft->forward(timeA.data(), designSpec.data());
        float peak = 0.0f;
        for (int k = 0; k <= half; ++k)
            peak = std::max(peak, std::abs(designSpec[static_cast<size_t>(k)]));
        if (peak > kSilencePeak) {
            const float s = 1.0f / peak;
            for (int i = 0; i < L; ++i)
                tp[i] *= s;
        }
    }
};

// ------------------------------------------------------------------- facade
KernelGenerator::KernelGenerator() : impl_(std::make_unique<Impl>()) {}
KernelGenerator::~KernelGenerator() = default;
KernelGenerator::KernelGenerator(KernelGenerator&&) noexcept = default;
KernelGenerator& KernelGenerator::operator=(KernelGenerator&&) noexcept = default;

void KernelGenerator::prepare(const Config& config) {
    impl_->prepare(config);
}

void KernelGenerator::setWavetable(const WavetableData* table) noexcept {
    if (impl_ != nullptr)
        impl_->table = table;
}

void KernelGenerator::generate(const KernelRequest& request, Kernel& out) noexcept {
    if (impl_ != nullptr)
        impl_->generate(request, out);
}

int KernelGenerator::latencyForMode(PhaseMode mode, int kernelLength) noexcept {
    return (mode == PhaseMode::Linear || mode == PhaseMode::Original) ? kernelLength / 2 : 0;
}

} // namespace ftc
