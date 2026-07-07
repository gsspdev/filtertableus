#include "wavetable/SampleConverter.h"

#include <algorithm>
#include <cmath>

#include "wavetable/FrameOps.h"

namespace ftus {

namespace {

/// YIN on one analysis frame. Returns f0 in Hz, or 0 when unvoiced.
double yinFrameF0(const float* x, double fs) {
    constexpr int window = SampleConverter::kYinWindow;
    const int maxTau = std::min(window, static_cast<int>(std::floor(fs / SampleConverter::kMinF0)));
    const int minTau = std::max(2, static_cast<int>(std::ceil(fs / SampleConverter::kMaxF0)));
    if (maxTau <= minTau + 2)
        return 0.0;

    // Difference function d(tau) over a fixed window (frame is window + maxTau <= 4096).
    std::vector<float> d(static_cast<size_t>(maxTau) + 1, 0.0f);
    for (int tau = 1; tau <= maxTau; ++tau) {
        double acc = 0.0;
        const float* a = x;
        const float* b = x + tau;
        for (int j = 0; j < window; ++j) {
            const float diff = a[j] - b[j];
            acc += static_cast<double>(diff) * static_cast<double>(diff);
        }
        d[static_cast<size_t>(tau)] = static_cast<float>(acc);
    }

    // Cumulative-mean-normalized difference d'(tau).
    std::vector<float> dn(static_cast<size_t>(maxTau) + 1, 1.0f);
    double running = 0.0;
    for (int tau = 1; tau <= maxTau; ++tau) {
        running += static_cast<double>(d[static_cast<size_t>(tau)]);
        dn[static_cast<size_t>(tau)] =
            running > 0.0
                ? static_cast<float>(static_cast<double>(d[static_cast<size_t>(tau)]) *
                                     static_cast<double>(tau) / running)
                : 1.0f;
    }

    // Absolute threshold: first dip below threshold, then walk down to its local minimum.
    int tauEst = -1;
    for (int tau = minTau; tau <= maxTau; ++tau) {
        if (dn[static_cast<size_t>(tau)] < SampleConverter::kYinThreshold) {
            while (tau + 1 <= maxTau && dn[static_cast<size_t>(tau + 1)] < dn[static_cast<size_t>(tau)])
                ++tau;
            tauEst = tau;
            break;
        }
    }
    if (tauEst < 0)
        return 0.0; // unvoiced

    // Parabolic interpolation around the minimum.
    double tauF = static_cast<double>(tauEst);
    if (tauEst > minTau && tauEst < maxTau) {
        const double dm = static_cast<double>(dn[static_cast<size_t>(tauEst - 1)]);
        const double d0 = static_cast<double>(dn[static_cast<size_t>(tauEst)]);
        const double dp = static_cast<double>(dn[static_cast<size_t>(tauEst + 1)]);
        const double denom = dm - 2.0 * d0 + dp;
        if (std::abs(denom) > 1.0e-12) {
            const double delta = 0.5 * (dm - dp) / denom;
            if (std::abs(delta) <= 1.0)
                tauF += delta;
        }
    }

    const double f0 = fs / tauF;
    if (f0 < SampleConverter::kMinF0 || f0 > SampleConverter::kMaxF0)
        return 0.0;
    return f0;
}

/// Median of the voiced (> 0) values in a window of the track centred at i; 0 if none.
double medianFilteredAt(const std::vector<double>& track, int i, int width) {
    const int half = width / 2;
    const int n = static_cast<int>(track.size());
    std::vector<double> voiced;
    voiced.reserve(static_cast<size_t>(width));
    for (int j = std::max(0, i - half); j <= std::min(n - 1, i + half); ++j)
        if (track[static_cast<size_t>(j)] > 0.0)
            voiced.push_back(track[static_cast<size_t>(j)]);
    if (voiced.empty())
        return 0.0;
    const size_t mid = voiced.size() / 2;
    std::nth_element(voiced.begin(), voiced.begin() + static_cast<long>(mid), voiced.end());
    return voiced[mid];
}

double medianOf(std::vector<double> v) {
    if (v.empty())
        return 0.0;
    const size_t mid = v.size() / 2;
    std::nth_element(v.begin(), v.begin() + static_cast<long>(mid), v.end());
    return v[mid];
}

} // namespace

juce::String SampleConverter::pitchErrorMessage() {
    return juce::String::fromUTF8(
        "Couldn't detect a pitch \xe2\x80\x94 try a 2048-multiple wavetable or a short "
        "single-cycle file");
}

ConvertResult SampleConverter::convert(std::span<const float> mono, double sampleRate) {
    ConvertResult result;
    const int len = static_cast<int>(mono.size());
    if (len < kYinFrame + 1 || sampleRate <= 0.0) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }

    // Work on a mean-removed copy so zero-crossing slicing is DC-safe.
    std::vector<float> x(mono.begin(), mono.end());
    {
        double sum = 0.0;
        for (const float v : x)
            sum += static_cast<double>(v);
        const float mean = static_cast<float>(sum / static_cast<double>(x.size()));
        for (float& v : x)
            v -= mean;
    }

    // --- YIN track ------------------------------------------------------------------
    std::vector<int> starts;
    for (int s = 0; s + kYinFrame <= len; s += kYinHop)
        starts.push_back(s);
    if (static_cast<int>(starts.size()) > kMaxAnalysisFrames) {
        const auto keep =
            wtio::evenStrideIndices(static_cast<int>(starts.size()), kMaxAnalysisFrames);
        std::vector<int> strided;
        strided.reserve(keep.size());
        for (const int k : keep)
            strided.push_back(starts[static_cast<size_t>(k)]);
        starts = std::move(strided);
    }

    std::vector<double> track(starts.size(), 0.0);
    for (size_t i = 0; i < starts.size(); ++i)
        track[i] = yinFrameF0(x.data() + starts[i], sampleRate);

    // Median filter (width 5) over voiced neighbours, then global median.
    std::vector<double> filtered(track.size(), 0.0);
    int voicedCount = 0;
    std::vector<double> voicedValues;
    for (int i = 0; i < static_cast<int>(track.size()); ++i) {
        if (track[static_cast<size_t>(i)] <= 0.0)
            continue; // an unvoiced frame stays unvoiced
        filtered[static_cast<size_t>(i)] = medianFilteredAt(track, i, kMedianWidth);
        if (filtered[static_cast<size_t>(i)] > 0.0) {
            ++voicedCount;
            voicedValues.push_back(filtered[static_cast<size_t>(i)]);
        }
    }

    const float voicedRatio =
        track.empty() ? 0.0f
                      : static_cast<float>(voicedCount) / static_cast<float>(track.size());
    if (voicedRatio < kMinVoicedRatio || voicedCount < 2) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }

    const double f0 = medianOf(voicedValues);
    if (f0 < kMinF0 || f0 > kMaxF0) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }
    const double period = sampleRate / f0;
    if (period < 2.0 || period > static_cast<double>(len) / 2.0) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }

    // --- Slice at the strongest rising zero-crossing ---------------------------------
    // Anchor selection: find the first usable rising crossing, then take the STRONGEST
    // rising crossing within one period of it. (A global argmax would let noise pick an
    // anchor near the end of the file and throw away almost every period; one period's
    // window still contains every distinct crossing type of the cycle.)
    const int lastUsableStart = static_cast<int>(std::floor(static_cast<double>(len - 1) - period));
    if (lastUsableStart < 0) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }
    auto isRisingCrossing = [&x](int i) {
        return x[static_cast<size_t>(i)] < 0.0f && x[static_cast<size_t>(i + 1)] >= 0.0f;
    };
    int firstCrossing = -1;
    for (int i = 0; i <= lastUsableStart; ++i) {
        if (isRisingCrossing(i)) {
            firstCrossing = i;
            break;
        }
    }
    if (firstCrossing < 0) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }
    const int windowEnd = std::min(
        lastUsableStart, firstCrossing + static_cast<int>(std::floor(period)));
    int bestCrossing = firstCrossing;
    float bestStrength = 0.0f;
    for (int i = firstCrossing; i <= windowEnd; ++i) {
        if (isRisingCrossing(i)) {
            const float strength = x[static_cast<size_t>(i + 1)] - x[static_cast<size_t>(i)];
            if (strength > bestStrength) {
                bestStrength = strength;
                bestCrossing = i;
            }
        }
    }

    const int numPeriods = static_cast<int>(
        std::floor((static_cast<double>(len - 1) - static_cast<double>(bestCrossing)) / period));
    if (numPeriods < 1) {
        result.errorMessage = pitchErrorMessage();
        return result;
    }

    const int outFrames = std::min(numPeriods, wtio::kMaxFrames);
    const auto periodIndices = wtio::evenStrideIndices(numPeriods, outFrames);

    result.frames.resize(static_cast<size_t>(outFrames) * wtio::kFrameLength);
    for (int f = 0; f < outFrames; ++f) {
        const double base =
            static_cast<double>(bestCrossing) + static_cast<double>(periodIndices[static_cast<size_t>(f)]) * period;
        float* frame = result.frames.data() + static_cast<size_t>(f) * wtio::kFrameLength;
        for (int j = 0; j < wtio::kFrameLength; ++j) {
            const double p = base + (static_cast<double>(j) * period) /
                                        static_cast<double>(wtio::kFrameLength);
            frame[j] = wtio::catmullRomClamped(x, p);
        }
        wtio::postProcessFrame({frame, static_cast<size_t>(wtio::kFrameLength)});
    }

    result.ok = true;
    result.numFrames = outFrames;
    result.detectedF0 = f0;
    return result;
}

} // namespace ftus
