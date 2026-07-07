// FilterTableUS core — real FFT wrapper over vendored pffft. FROZEN after Phase 0.
//
// Threading: a RealFFT instance is stateless per call except its work area, so a single
// instance must not be used from two threads concurrently. Construct in prepare(); all
// per-call methods are RT-safe (no allocation).
//
// Scaling conventions:
//   forward()  : ordered spectrum, N/2+1 bins, NOT scaled.
//   inverse()  : time output scaled by 1/N (forward -> inverse round-trips exactly).
//   forwardZ() : pffft internal ("z") domain, N floats, NOT scaled.
//   inverseZ() : NOT scaled — fold 1/N into zconvolveAccumulate's scale parameter.
//   zconvolveAccumulate(): acc += scale * (a (x) b) in the z domain (the partitioned-
//                          convolution inner loop; handles packed DC/Nyquist correctly).
#pragma once
#include <complex>
#include "ftc/AlignedVector.h"

struct PFFFT_Setup; // vendored pffft forward declaration

namespace ftc {

class RealFFT {
public:
    /// size must be a pffft-legal real-FFT size: a power of two >= 32 in this project
    /// (2048/4096/8192/16384/32768 are the sizes we use). Asserts/throws otherwise.
    explicit RealFFT(int size);
    ~RealFFT();
    RealFFT(RealFFT&& other) noexcept;
    RealFFT& operator=(RealFFT&& other) noexcept;
    RealFFT(const RealFFT&) = delete;
    RealFFT& operator=(const RealFFT&) = delete;

    int size() const noexcept { return size_; }
    /// Number of floats in a z-domain buffer (== size()).
    int zSize() const noexcept { return size_; }

    void forward(const float* time, std::complex<float>* spectrum) const noexcept; // N/2+1 bins
    void inverse(const std::complex<float>* spectrum, float* time) const noexcept; // scaled 1/N

    void forwardZ(const float* time, float* z) const noexcept;
    void inverseZ(const float* z, float* time) const noexcept; // unscaled
    void zconvolveAccumulate(const float* a, const float* b, float* acc, float scale) const noexcept;

private:
    int size_ = 0;
    PFFFT_Setup* setup_ = nullptr;
    mutable AlignedVector<float> work_;
    mutable AlignedVector<float> packed_; // scratch for ordered<->packed conversion
};

} // namespace ftc
