// FilterTableUS core — vectorizable transcendental approximations for kernel math.
// FROZEN after Phase 0 (implementations may be optimized later behind these signatures).
//
// Accuracy contracts (tested):
//   logAbsApprox : |absolute err in the returned log value| < 5e-4 for |x| in [1e-6, 1e6]
//                  (i.e. < 0.005 dB); inputs with |x| <= 1e-6 clamp to ln(1e-6).
//   expApprox    : |rel err| < 2e-4 for x in [-30, +30]; input clamped to [-87, +87].
//   expComplex   : magnitude via expApprox contract; phase via libm sin/cos (exact).
// All functions are plain loops over contiguous arrays — safe on the audio thread, no allocation.
#pragma once
#include <complex>

namespace ftc {

/// out[i] = ln(max(|in[i]|, 1e-6))
void logAbsApprox(const float* in, float* out, int n) noexcept;

/// out[i] = e^{in[i]}   (input clamped to [-87, +87] to stay finite in float)
void expApprox(const float* in, float* out, int n) noexcept;

/// H[i] = e^{lnH[i].re} * (cos(lnH[i].im), sin(lnH[i].im))   — complex exp of a log-spectrum.
void expComplex(const std::complex<float>* lnH, std::complex<float>* H, int n) noexcept;

} // namespace ftc
