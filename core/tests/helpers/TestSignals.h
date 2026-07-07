// Shared test helpers: analytic wavetable frames, naive DFT/convolution references,
// kernel response probe, click detector. Used by every core test suite.
#pragma once
#include <complex>
#include <span>
#include <vector>

namespace ftt {

constexpr int kFrameLength = 2048;

/// One 2048-sample cycle of sin(2*pi*harmonic*n/2048) * amp.
std::vector<float> makeSineFrame(int harmonic, float amp = 1.0f);

/// Saw-like frame: sum_{k=1}^{numHarmonics} (1/k) sin(2*pi*k*n/N).
std::vector<float> makeSawFrame(int numHarmonics = 512);

/// Square-like frame: odd harmonics at 1/k.
std::vector<float> makeSquareFrame(int numHarmonics = 511);

/// Two-frame table: frame 0 = dark (harmonics 1..8), frame 1 = bright (harmonics 24..64).
std::vector<float> makeTwoFrameMorphTable();

/// O(n^2) DFT reference (real input, N/2+1 bins, unscaled — matches ftc::RealFFT::forward).
std::vector<std::complex<float>> naiveDFT(std::span<const float> x);

/// Full linear convolution y[n] = sum x[m] h[n-m]; length x.size()+h.size()-1.
std::vector<float> naiveConvolve(std::span<const float> x, std::span<const float> h);

/// |FFT(taps zero-padded to nfft)| — magnitude response probe, nfft/2+1 points.
/// Frequency of point k at sample rate fs is k*fs/nfft.
std::vector<float> measureMagnitudeResponse(std::span<const float> taps, int nfft);

/// Max over n of |y[n] - 2y[n-1] + y[n-2]| / (local RMS + eps): a click/zipper score.
/// Smooth signals score low; discontinuities spike.
float clickScore(std::span<const float> y, int rmsWindow = 256);

} // namespace ftt
