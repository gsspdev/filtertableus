#include "ftc/FastMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace ftc {

namespace {

constexpr float kLn2 = 0.69314718055994530942f;
constexpr float kInvLn2 = 1.44269504088896340736f;
constexpr float kMinAbs = 1e-6f;

inline float bitsToFloat(std::uint32_t b) noexcept {
    float f;
    std::memcpy(&f, &b, sizeof(f));
    return f;
}
inline std::uint32_t floatToBits(float f) noexcept {
    std::uint32_t b;
    std::memcpy(&b, &f, sizeof(b));
    return b;
}

// ln(x) for x > 0: exponent/mantissa split, range-reduce mantissa to [1/sqrt2, sqrt2),
// then a 7-term ln(1+t) Taylor series (|t| <= 0.4143 -> abs error < ~1.1e-4).
inline float fastLn(float x) noexcept {
    std::uint32_t bits = floatToBits(x);
    int e = static_cast<int>((bits >> 23) & 255u) - 127;
    bits = (bits & 0x007FFFFFu) | 0x3F800000u; // mantissa in [1, 2)
    float m = bitsToFloat(bits);
    if (m >= 1.41421356f) {
        m *= 0.5f;
        e += 1;
    }
    const float t = m - 1.0f;
    // t - t^2/2 + t^3/3 - t^4/4 + t^5/5 - t^6/6 + t^7/7  (Horner)
    const float p = t * (1.0f + t * (-0.5f + t * (0.33333333f + t * (-0.25f
                    + t * (0.2f + t * (-0.16666667f + t * 0.14285714f))))));
    return static_cast<float>(e) * kLn2 + p;
}

// e^w for |w| <= ln2/2 via 6-term Taylor (abs error < 4e-7 in range).
inline float expPoly(float w) noexcept {
    return 1.0f + w * (1.0f + w * (0.5f + w * (0.16666667f + w * (0.041666667f
           + w * (0.0083333333f + w * 0.0013888889f)))));
}

// e^x = 2^i * e^{f*ln2}, i = round(x/ln2)
inline float fastExp(float x) noexcept {
    x = x < -87.0f ? -87.0f : (x > 87.0f ? 87.0f : x);
    const float t = x * kInvLn2;
    const float ri = t >= 0.0f ? static_cast<float>(static_cast<int>(t + 0.5f))
                               : static_cast<float>(static_cast<int>(t - 0.5f));
    const float w = (t - ri) * kLn2;
    const std::uint32_t scaleBits = static_cast<std::uint32_t>(static_cast<int>(ri) + 127) << 23;
    return bitsToFloat(scaleBits) * expPoly(w);
}

} // namespace

void logAbsApprox(const float* in, float* out, int n) noexcept {
    for (int i = 0; i < n; ++i) {
        float a = in[i] < 0.0f ? -in[i] : in[i];
        if (a < kMinAbs)
            a = kMinAbs;
        out[i] = fastLn(a);
    }
}

void expApprox(const float* in, float* out, int n) noexcept {
    for (int i = 0; i < n; ++i)
        out[i] = fastExp(in[i]);
}

void expComplex(const std::complex<float>* lnH, std::complex<float>* H, int n) noexcept {
    for (int i = 0; i < n; ++i) {
        const float mag = fastExp(lnH[i].real());
        const float ph = lnH[i].imag();
        H[i] = {mag * std::cos(ph), mag * std::sin(ph)};
    }
}

} // namespace ftc
