#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <random>

#include "ftc/AlignedVector.h"
#include "ftc/FFT.h"
#include "helpers/TestSignals.h"

using ftc::AlignedVector;
using ftc::RealFFT;

namespace {
std::vector<float> randomSignal(int n, unsigned seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> v(static_cast<size_t>(n));
    for (auto& x : v)
        x = dist(rng);
    return v;
}
} // namespace

TEST_CASE("RealFFT round-trips", "[fft]") {
    for (int size : {32, 256, 2048}) {
        auto x = randomSignal(size, 42u + static_cast<unsigned>(size));
        RealFFT fft(size);
        std::vector<std::complex<float>> spec(static_cast<size_t>(size / 2 + 1));
        std::vector<float> back(static_cast<size_t>(size));
        fft.forward(x.data(), spec.data());
        fft.inverse(spec.data(), back.data());
        for (int i = 0; i < size; ++i)
            REQUIRE_THAT(back[static_cast<size_t>(i)],
                         Catch::Matchers::WithinAbs(x[static_cast<size_t>(i)], 1e-4));
    }
}

TEST_CASE("RealFFT matches naive DFT", "[fft]") {
    const int size = 64;
    auto x = randomSignal(size, 7u);
    RealFFT fft(size);
    std::vector<std::complex<float>> spec(static_cast<size_t>(size / 2 + 1));
    fft.forward(x.data(), spec.data());
    auto ref = ftt::naiveDFT(x);
    for (size_t k = 0; k < ref.size(); ++k) {
        REQUIRE_THAT(spec[k].real(), Catch::Matchers::WithinAbs(ref[k].real(), 1e-3));
        REQUIRE_THAT(spec[k].imag(), Catch::Matchers::WithinAbs(ref[k].imag(), 1e-3));
    }
}

TEST_CASE("RealFFT Parseval", "[fft]") {
    const int size = 512;
    auto x = randomSignal(size, 11u);
    RealFFT fft(size);
    std::vector<std::complex<float>> spec(static_cast<size_t>(size / 2 + 1));
    fft.forward(x.data(), spec.data());
    double timeEnergy = 0.0;
    for (float v : x)
        timeEnergy += static_cast<double>(v) * v;
    double freqEnergy = std::norm(spec[0]) + std::norm(spec[static_cast<size_t>(size / 2)]);
    for (int k = 1; k < size / 2; ++k)
        freqEnergy += 2.0 * std::norm(spec[static_cast<size_t>(k)]);
    freqEnergy /= size;
    REQUIRE_THAT(freqEnergy, Catch::Matchers::WithinRel(timeEnergy, 1e-4));
}

TEST_CASE("z-domain convolve-accumulate equals circular convolution", "[fft]") {
    const int size = 256;
    auto a = randomSignal(size, 1u);
    auto b = randomSignal(size, 2u);
    RealFFT fft(size);

    // Reference: circular convolution via ordered spectra.
    std::vector<std::complex<float>> A(static_cast<size_t>(size / 2 + 1));
    std::vector<std::complex<float>> B(A.size()), C(A.size());
    fft.forward(a.data(), A.data());
    fft.forward(b.data(), B.data());
    for (size_t k = 0; k < A.size(); ++k)
        C[k] = A[k] * B[k];
    std::vector<float> ref(static_cast<size_t>(size));
    fft.inverse(C.data(), ref.data());

    // z-domain path: forwardZ + zconvolveAccumulate(scale = 1/N) + inverseZ.
    AlignedVector<float> za(static_cast<size_t>(size)), zb(static_cast<size_t>(size)),
        zacc(static_cast<size_t>(size), 0.0f);
    fft.forwardZ(a.data(), za.data());
    fft.forwardZ(b.data(), zb.data());
    fft.zconvolveAccumulate(za.data(), zb.data(), zacc.data(), 1.0f / static_cast<float>(size));
    std::vector<float> got(static_cast<size_t>(size));
    fft.inverseZ(zacc.data(), got.data());

    for (int i = 0; i < size; ++i)
        REQUIRE_THAT(got[static_cast<size_t>(i)],
                     Catch::Matchers::WithinAbs(ref[static_cast<size_t>(i)], 1e-3));
}
