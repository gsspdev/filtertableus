// FilterTableUS core — wavetable frame -> FIR kernel generation. FROZEN after Phase 0.
// Implemented by the kernel-generation workstream (core/src/kernel*.cpp); see docs/PLAN.md §2:
// scan interpolation, harmonic-24 cutoff mapping, resonance contrast, and the four
// phase-mode reconstructions (Minimum: cepstral min-phase; Linear: zero-phase + Tukey;
// Original: band-limited cyclic, centered L/2; Raw: causal cyclic, unfiltered).
//
// Threading: prepare()/setWavetable() from a non-audio thread OR the audio thread while not
// concurrently generating; generate() is RT-safe (no allocation, no locks) and is called
// inline on the audio thread at kernel ticks. The WavetableData pointer passed to
// setWavetable must outlive use (guaranteed by ObjectHandoff's pending/current/previous GC).
#pragma once
#include <memory>
#include "ftc/Kernel.h"
#include "ftc/Types.h"

namespace ftc {

class WavetableData;

class KernelGenerator {
public:
    struct Config {
        double sampleRate = 48000.0;
        int kernelLength = 2048;      // EngineConfig::kernelLength(fs)
    };

    KernelGenerator();
    ~KernelGenerator();
    KernelGenerator(KernelGenerator&&) noexcept;
    KernelGenerator& operator=(KernelGenerator&&) noexcept;

    /// Allocates FFT plans and scratch. Not RT-safe.
    void prepare(const Config& config);

    /// Non-owning table swap. RT-safe. nullptr = no table (generate() then produces a
    /// pass-through kernel: unit impulse for latency-0 modes, centered impulse for L/2 modes).
    void setWavetable(const WavetableData* table) noexcept;

    /// Build an L-tap kernel for the request. RT-safe after prepare(). Sets out's length
    /// and latency (0 for Minimum/Raw, L/2 for Linear/Original).
    void generate(const KernelRequest& request, Kernel& out) noexcept;

    static int latencyForMode(PhaseMode mode, int kernelLength) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ftc
