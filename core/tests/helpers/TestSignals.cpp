#include "helpers/TestSignals.h"

#include <cmath>

#include "ftc/FFT.h"

namespace ftt {

namespace {
constexpr double kTwoPi = 6.283185307179586476925286766559;
}

std::vector<float> makeSineFrame(int harmonic, float amp) {
    std::vector<float> v(kFrameLength);
    for (int n = 0; n < kFrameLength; ++n)
        v[static_cast<size_t>(n)] =
            amp * static_cast<float>(std::sin(kTwoPi * harmonic * n / kFrameLength));
    return v;
}

std::vector<float> makeSawFrame(int numHarmonics) {
    std::vector<float> v(kFrameLength, 0.0f);
    for (int k = 1; k <= numHarmonics; ++k)
        for (int n = 0; n < kFrameLength; ++n)
            v[static_cast<size_t>(n)] +=
                static_cast<float>(std::sin(kTwoPi * k * n / kFrameLength) / k);
    return v;
}

std::vector<float> makeSquareFrame(int numHarmonics) {
    std::vector<float> v(kFrameLength, 0.0f);
    for (int k = 1; k <= numHarmonics; k += 2)
        for (int n = 0; n < kFrameLength; ++n)
            v[static_cast<size_t>(n)] +=
                static_cast<float>(std::sin(kTwoPi * k * n / kFrameLength) / k);
    return v;
}

std::vector<float> makeTwoFrameMorphTable() {
    std::vector<float> v(2 * kFrameLength, 0.0f);
    for (int k = 1; k <= 8; ++k)
        for (int n = 0; n < kFrameLength; ++n)
            v[static_cast<size_t>(n)] +=
                static_cast<float>(std::sin(kTwoPi * k * n / kFrameLength) / k);
    for (int k = 24; k <= 64; ++k)
        for (int n = 0; n < kFrameLength; ++n)
            v[static_cast<size_t>(kFrameLength + n)] +=
                static_cast<float>(std::sin(kTwoPi * k * n / kFrameLength) / k);
    return v;
}

std::vector<std::complex<float>> naiveDFT(std::span<const float> x) {
    const int n = static_cast<int>(x.size());
    std::vector<std::complex<float>> out(static_cast<size_t>(n / 2 + 1));
    for (int k = 0; k <= n / 2; ++k) {
        double re = 0.0, im = 0.0;
        for (int m = 0; m < n; ++m) {
            const double ph = kTwoPi * k * m / n;
            re += x[static_cast<size_t>(m)] * std::cos(ph);
            im -= x[static_cast<size_t>(m)] * std::sin(ph);
        }
        out[static_cast<size_t>(k)] = {static_cast<float>(re), static_cast<float>(im)};
    }
    return out;
}

std::vector<float> naiveConvolve(std::span<const float> x, std::span<const float> h) {
    const int nx = static_cast<int>(x.size());
    const int nh = static_cast<int>(h.size());
    std::vector<float> y(static_cast<size_t>(nx + nh - 1), 0.0f);
    for (int i = 0; i < nx; ++i)
        for (int j = 0; j < nh; ++j)
            y[static_cast<size_t>(i + j)] += x[static_cast<size_t>(i)] * h[static_cast<size_t>(j)];
    return y;
}

std::vector<float> measureMagnitudeResponse(std::span<const float> taps, int nfft) {
    std::vector<float> padded(static_cast<size_t>(nfft), 0.0f);
    const size_t n = taps.size() < padded.size() ? taps.size() : padded.size();
    for (size_t i = 0; i < n; ++i)
        padded[i] = taps[i];
    ftc::RealFFT fft(nfft);
    std::vector<std::complex<float>> spec(static_cast<size_t>(nfft / 2 + 1));
    fft.forward(padded.data(), spec.data());
    std::vector<float> mags(spec.size());
    for (size_t i = 0; i < spec.size(); ++i)
        mags[i] = std::abs(spec[i]);
    return mags;
}

float clickScore(std::span<const float> y, int rmsWindow) {
    const int n = static_cast<int>(y.size());
    if (n < 3)
        return 0.0f;
    // running RMS over rmsWindow
    double acc = 0.0;
    float worst = 0.0f;
    std::vector<float> sq(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i)
        sq[static_cast<size_t>(i)] = y[static_cast<size_t>(i)] * y[static_cast<size_t>(i)];
    for (int i = 0; i < n; ++i) {
        acc += sq[static_cast<size_t>(i)];
        if (i >= rmsWindow)
            acc -= sq[static_cast<size_t>(i - rmsWindow)];
        if (i < 2)
            continue;
        const int count = i < rmsWindow ? i + 1 : rmsWindow;
        const float rms = static_cast<float>(std::sqrt(acc / count));
        const float d2 = std::fabs(y[static_cast<size_t>(i)] - 2.0f * y[static_cast<size_t>(i - 1)]
                                   + y[static_cast<size_t>(i - 2)]);
        const float score = d2 / (rms + 1e-6f);
        if (score > worst)
            worst = score;
    }
    return worst;
}

} // namespace ftt
