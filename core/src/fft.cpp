#include "ftc/FFT.h"

#include <cstring>
#include <stdexcept>

#include "pffft/pffft.h"

namespace ftc {

namespace {
bool isValidSize(int n) noexcept { return n >= 32 && (n & (n - 1)) == 0; }
} // namespace

RealFFT::RealFFT(int size) : size_(size) {
    if (!isValidSize(size))
        throw std::invalid_argument("RealFFT: size must be a power of two >= 32");
    setup_ = pffft_new_setup(size, PFFFT_REAL);
    if (setup_ == nullptr)
        throw std::runtime_error("RealFFT: pffft_new_setup failed");
    work_.assign(static_cast<size_t>(size_), 0.0f);
    packed_.assign(static_cast<size_t>(size_), 0.0f);
}

RealFFT::~RealFFT() {
    if (setup_ != nullptr)
        pffft_destroy_setup(setup_);
}

RealFFT::RealFFT(RealFFT&& other) noexcept
    : size_(other.size_), setup_(other.setup_),
      work_(std::move(other.work_)), packed_(std::move(other.packed_)) {
    other.setup_ = nullptr;
    other.size_ = 0;
}

RealFFT& RealFFT::operator=(RealFFT&& other) noexcept {
    if (this != &other) {
        if (setup_ != nullptr)
            pffft_destroy_setup(setup_);
        size_ = other.size_;
        setup_ = other.setup_;
        work_ = std::move(other.work_);
        packed_ = std::move(other.packed_);
        other.setup_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

// pffft ordered real layout: [X0.re, XN/2.re, X1.re, X1.im, X2.re, X2.im, ...]
void RealFFT::forward(const float* time, std::complex<float>* spectrum) const noexcept {
    std::memcpy(packed_.data(), time, sizeof(float) * static_cast<size_t>(size_));
    pffft_transform_ordered(setup_, packed_.data(), packed_.data(), work_.data(), PFFFT_FORWARD);
    const int half = size_ / 2;
    spectrum[0] = {packed_[0], 0.0f};
    spectrum[half] = {packed_[1], 0.0f};
    for (int k = 1; k < half; ++k)
        spectrum[k] = {packed_[2 * static_cast<size_t>(k)], packed_[2 * static_cast<size_t>(k) + 1]};
}

void RealFFT::inverse(const std::complex<float>* spectrum, float* time) const noexcept {
    const int half = size_ / 2;
    packed_[0] = spectrum[0].real();
    packed_[1] = spectrum[half].real();
    for (int k = 1; k < half; ++k) {
        packed_[2 * static_cast<size_t>(k)] = spectrum[k].real();
        packed_[2 * static_cast<size_t>(k) + 1] = spectrum[k].imag();
    }
    pffft_transform_ordered(setup_, packed_.data(), packed_.data(), work_.data(), PFFFT_BACKWARD);
    const float scale = 1.0f / static_cast<float>(size_);
    for (int i = 0; i < size_; ++i)
        time[i] = packed_[static_cast<size_t>(i)] * scale;
}

void RealFFT::forwardZ(const float* time, float* z) const noexcept {
    std::memcpy(packed_.data(), time, sizeof(float) * static_cast<size_t>(size_));
    pffft_transform(setup_, packed_.data(), z, work_.data(), PFFFT_FORWARD);
}

void RealFFT::inverseZ(const float* z, float* time) const noexcept {
    pffft_transform(setup_, z, packed_.data(), work_.data(), PFFFT_BACKWARD);
    std::memcpy(time, packed_.data(), sizeof(float) * static_cast<size_t>(size_));
}

void RealFFT::zconvolveAccumulate(const float* a, const float* b, float* acc, float scale) const noexcept {
    pffft_zconvolve_accumulate(setup_, a, b, acc, scale);
}

} // namespace ftc
