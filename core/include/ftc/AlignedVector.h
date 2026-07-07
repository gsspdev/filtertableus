// FilterTableUS core — 64-byte-aligned vector for DSP buffers. FROZEN after Phase 0.
#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#include <vector>

namespace ftc {

template <class T, std::size_t Alignment = 64>
struct AlignedAllocator {
    using value_type = T;
    static constexpr std::align_val_t alignment{Alignment};

    AlignedAllocator() noexcept = default;
    template <class U> AlignedAllocator(const AlignedAllocator<U, Alignment>&) noexcept {}

    T* allocate(std::size_t n) {
        return static_cast<T*>(::operator new(n * sizeof(T), alignment));
    }
    void deallocate(T* p, std::size_t) noexcept { ::operator delete(p, alignment); }

    template <class U> struct rebind { using other = AlignedAllocator<U, Alignment>; };
    friend bool operator==(const AlignedAllocator&, const AlignedAllocator&) { return true; }
};

/// std::vector with 64-byte-aligned storage (SIMD/pffft-safe; pffft needs >= 16).
template <class T>
using AlignedVector = std::vector<T, AlignedAllocator<T>>;

} // namespace ftc
