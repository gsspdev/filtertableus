// FilterTableUS core — zero-latency partitioned convolution. FROZEN after Phase 0.
// Implemented by the convolution workstream (core/src/convolver*.cpp); see docs/PLAN.md §2:
// direct-FIR head (EngineConfig::headLength taps) + uniformly partitioned frequency-domain
// tail (EngineConfig::partitionLength, Gardner scheduling — zero added latency), dual-instance
// output crossfade for click-free time-varying kernels.
//
// Threading: prepare() allocates (not RT-safe); everything else is RT-safe and single-threaded
// (the audio thread). KernelImage is an opaque analyzed kernel (head taps + partition spectra);
// its latencySamples() carries the source Kernel's latency.
#pragma once
#include <memory>
#include "ftc/Kernel.h"

namespace ftc {

class PartitionedConvolver;

class KernelImage {
public:
    KernelImage();
    ~KernelImage();
    KernelImage(KernelImage&&) noexcept;
    KernelImage& operator=(KernelImage&&) noexcept;

    /// Allocates worst-case storage. Not RT-safe.
    void prepare(int maxKernelLength, int headLength, int partitionLength);
    int latencySamples() const noexcept;

private:
    friend class PartitionedConvolver;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Mono zero-latency hybrid convolver.
class PartitionedConvolver {
public:
    struct Config {
        int maxKernelLength = 2048;
        int headLength = 128;
        int partitionLength = 128;
        int maxBlockSize = 8192;
    };

    PartitionedConvolver();
    ~PartitionedConvolver();
    PartitionedConvolver(PartitionedConvolver&&) noexcept;
    PartitionedConvolver& operator=(PartitionedConvolver&&) noexcept;

    void prepare(const Config& config);                                   // allocates
    void analyze(const Kernel& kernel, KernelImage& out) const noexcept;  // RT-safe partition FFTs
    void reset() noexcept;                                                // zero all state
    void copyStateFrom(const PartitionedConvolver& other) noexcept;       // bounded memcpy of history
    /// Convolve n samples (any n >= 1, independent of prepare-time block size).
    void process(const KernelImage& kernel, const float* in, float* out, int n) noexcept;

private:
    friend class KernelImage;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Multi-channel wrapper: per channel two convolver instances (A/B) with a per-sample linear
/// output crossfade between kernel images. Steady-state cost is one instance per channel.
class ConvolutionSection {
public:
    ConvolutionSection();
    ~ConvolutionSection();

    void prepare(const PartitionedConvolver::Config& config, int numChannels,
                 int fadeLengthSamples);                                  // allocates
    void reset() noexcept;

    /// Begin a crossfade to `kernel`. Returns false (and does nothing) while a fade is in
    /// flight — the caller retries at the next kernel tick.
    bool pushKernel(const Kernel& kernel) noexcept;
    /// Hard swap + state reset (mode switches; the caller masks the click with its own fade).
    void setKernelImmediate(const Kernel& kernel) noexcept;
    bool isFading() const noexcept;
    /// In-place wet processing of numChannels x n samples.
    void process(float* const* channels, int n) noexcept;
    int currentLatencySamples() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ftc
