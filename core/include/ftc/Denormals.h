// FilterTableUS core — scoped FTZ/DAZ denormal disabling. FROZEN after Phase 0.
// JUCE-free equivalent of juce::ScopedNoDenormals. Use on the audio thread (and any
// thread running kernel math). Restores the previous FP state on destruction.
#pragma once
#include <cstdint>

#if defined(__aarch64__) || defined(_M_ARM64)
  #define FTC_ARCH_ARM64 1
#elif defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
  #define FTC_ARCH_X86 1
  #include <immintrin.h>
#endif

namespace ftc {

class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept {
#if FTC_ARCH_ARM64
        std::uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        fpcr |= (1ull << 24); // FZ: flush-to-zero
        asm volatile("msr fpcr, %0" : : "r"(fpcr));
#elif FTC_ARCH_X86
        saved_ = _mm_getcsr();
        _mm_setcsr(saved_ | 0x8040u); // FTZ | DAZ
#endif
    }
    ~ScopedNoDenormals() noexcept {
#if FTC_ARCH_ARM64
        asm volatile("msr fpcr, %0" : : "r"(saved_));
#elif FTC_ARCH_X86
        _mm_setcsr(static_cast<std::uint32_t>(saved_));
#endif
    }
    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    std::uint64_t saved_ = 0;
};

} // namespace ftc
