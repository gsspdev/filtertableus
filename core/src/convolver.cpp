// FilterTableUS core — workstream A1: zero-latency hybrid partitioned convolution.
// Implements ftc::KernelImage, ftc::PartitionedConvolver, ftc::ConvolutionSection
// (declared in core/include/ftc/Convolver.h). See docs/PLAN.md §2.
//
// PartitionedConvolver (mono):
//  * Head: direct FIR over taps [0, headLength), evaluated per sample from a contiguous
//    input-history window. Taps are stored reversed in the KernelImage so the inner loop is
//    a straight dot product of two forward-contiguous arrays (8-way unrolled, fixed
//    summation order: deterministic and auto-vectorizable without -ffast-math).
//  * Tail: taps [headLength, L) in uniform partitions of partitionLength samples, each
//    zero-padded to a 2*partitionLength-point real FFT in pffft z-domain layout. Classic
//    frequency-domain delay line (FDL) of completed input-block spectra (50% overlap-save:
//    each spectrum covers [previous block | current block]). Tail partition j covers lags
//    [headLength + j*partitionLength, headLength + (j+1)*partitionLength), so with
//    headLength == q*partitionLength, output block B needs only spectra of input blocks
//    <= B - q — all fully completed in the past: Gardner scheduling, zero added latency,
//    correct for any call size n >= 1 via the internal partitionLength-sample FIFO.
//  * tailBuf caches the tail contribution for the current output block. The FDL itself is
//    kernel-independent; tailBuf is kernel-dependent, so every KernelImage carries a
//    revision stamp (bumped by analyze()) and process() recomputes tailBuf from the FDL
//    whenever the revision changes. This makes copyStateFrom() + processing with a
//    *different* kernel continue the input stream exactly (the crossfade case), while an
//    unchanged kernel pays nothing.
//
// Threading: prepare() allocates (not RT-safe). analyze()/reset()/copyStateFrom()/process()
// are RT-safe: no allocation, no locks, no exceptions; all buffers worst-case-sized in
// prepare(). A convolver instance is single-threaded (the audio thread); analyze() uses a
// mutable preallocated scratch, which is fine under that contract.

#include "ftc/Convolver.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>
#include <vector>

#include "ftc/AlignedVector.h"
#include "ftc/FFT.h"

namespace ftc {

namespace {

/// Global revision source for KernelImage stamps. Monotonic, so a (revision) match is
/// sufficient to prove "same analyzed content as the tail cache was built from".
std::atomic<std::uint64_t> gImageRevision{0};

inline int roundUpTo(int v, int multiple) noexcept {
    return ((v + multiple - 1) / multiple) * multiple;
}

/// Dot product with 8 independent accumulators, fixed combine order.
/// len must be a positive multiple of 8. Deterministic across calls/instances.
inline float dot8(const float* __restrict a, const float* __restrict b, int len) noexcept {
    float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
    float s4 = 0.0f, s5 = 0.0f, s6 = 0.0f, s7 = 0.0f;
    for (int i = 0; i < len; i += 8) {
        s0 += a[i] * b[i];
        s1 += a[i + 1] * b[i + 1];
        s2 += a[i + 2] * b[i + 2];
        s3 += a[i + 3] * b[i + 3];
        s4 += a[i + 4] * b[i + 4];
        s5 += a[i + 5] * b[i + 5];
        s6 += a[i + 6] * b[i + 6];
        s7 += a[i + 7] * b[i + 7];
    }
    return ((s0 + s1) + (s2 + s3)) + ((s4 + s5) + (s6 + s7));
}

} // namespace

// ============================== KernelImage ==================================

struct KernelImage::Impl {
    bool prepared = false;
    int headLength = 0;      // effective head length (normalized, see normalizeHead)
    int partitionLength = 0; // tail partition length
    int fftSize = 0;         // 2 * partitionLength
    int maxPartitions = 0;   // tail partition capacity
    int roundedHead = 0;     // headLength rounded up to a multiple of 8 (dot8 padding)
    int numPartitions = 0;   // active tail partitions for the analyzed kernel
    int kernelLength = 0;    // analyzed kernel length
    int latency = 0;         // carried from Kernel::latencySamples()
    std::uint64_t revision = 0;
    AlignedVector<float> headRev; // reversed head taps, zero-padded to roundedHead
    AlignedVector<float> spectra; // maxPartitions x fftSize z-domain partition spectra
};

namespace {
/// Gardner scheduling needs the head to span a whole positive number of partitions.
/// EngineConfig uses 128/128 (q = 1); this guard keeps degenerate configs well-defined.
inline int normalizeHead(int headLength, int partitionLength) noexcept {
    return roundUpTo(std::max(headLength, partitionLength), partitionLength);
}
} // namespace

KernelImage::KernelImage() : impl_(std::make_unique<Impl>()) {}
KernelImage::~KernelImage() = default;
KernelImage::KernelImage(KernelImage&&) noexcept = default;
KernelImage& KernelImage::operator=(KernelImage&&) noexcept = default;

void KernelImage::prepare(int maxKernelLength, int headLength, int partitionLength) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    Impl& img = *impl_;
    partitionLength = std::max(1, partitionLength);
    img.partitionLength = partitionLength;
    img.headLength = normalizeHead(std::max(1, headLength), partitionLength);
    img.fftSize = 2 * partitionLength;
    img.roundedHead = roundUpTo(img.headLength, 8);
    const int maxTail = std::max(0, maxKernelLength - img.headLength);
    img.maxPartitions = (maxTail + partitionLength - 1) / partitionLength;
    img.headRev.assign(static_cast<size_t>(img.roundedHead), 0.0f);
    img.spectra.assign(static_cast<size_t>(img.maxPartitions) * static_cast<size_t>(img.fftSize),
                       0.0f);
    img.numPartitions = 0;
    img.kernelLength = 0;
    img.latency = 0;
    img.revision = 0;
    img.prepared = true;
}

int KernelImage::latencySamples() const noexcept { return impl_ ? impl_->latency : 0; }

// =========================== PartitionedConvolver ============================

struct PartitionedConvolver::Impl {
    bool prepared = false;
    int headLength = 0;
    int partitionLength = 0;
    int fftSize = 0;
    int histLen = 0;       // headLength - 1 past samples kept for the head window
    int roundedHead = 0;   // headLength rounded up to a multiple of 8
    int maxPartitions = 0; // FDL/tail capacity
    int qOffset = 0;       // headLength / partitionLength (>= 1)
    int ringSize = 0;      // FDL slots: maxPartitions + qOffset - 1 (>= 1)
    std::optional<RealFFT> fft;

    // --- streaming state (everything copyStateFrom() transfers) ---
    AlignedVector<float> hist;    // histLen + partitionLength + 8 contiguous head window
    AlignedVector<float> timeSeg; // fftSize: [previous block | current (partial) block]
    AlignedVector<float> fdl;     // ringSize x fftSize input-block spectra (z-domain)
    AlignedVector<float> tailBuf; // partitionLength: tail output for current output block
    int fifoPos = 0;              // samples accumulated into the current input block
    int fdlHead = 0;              // ring index of the most recent completed block spectrum
    bool tailValid = false;
    std::uint64_t tailRevision = 0;

    // --- scratch (not part of the stream state) ---
    AlignedVector<float> zAcc;                   // fftSize accumulator (z-domain)
    AlignedVector<float> timeScratch;            // fftSize inverse-FFT output
    mutable AlignedVector<float> analyzeScratch; // fftSize (analyze() is const)

    /// tailBuf = last partitionLength samples of IFFT( sum_j FDL[recent - (j+q-1)] * H_j ).
    /// Valid both at a block boundary (for the next output block) and mid-block after a
    /// kernel change (for the current output block) — same formula, same FDL contents.
    void computeTail(const KernelImage::Impl& img) noexcept {
        const int parts = std::min(img.numPartitions, maxPartitions);
        if (parts <= 0) {
            std::memset(tailBuf.data(), 0, sizeof(float) * static_cast<size_t>(partitionLength));
            return;
        }
        std::memset(zAcc.data(), 0, sizeof(float) * static_cast<size_t>(fftSize));
        const float scale = 1.0f / static_cast<float>(fftSize); // fold 1/N here (FFT.h contract)
        for (int j = 0; j < parts; ++j) {
            int idx = fdlHead - (j + qOffset - 1);
            if (idx < 0)
                idx += ringSize; // j + qOffset - 1 < ringSize, so one wrap suffices
            fft->zconvolveAccumulate(fdl.data() + static_cast<size_t>(idx) * static_cast<size_t>(fftSize),
                                     img.spectra.data() + static_cast<size_t>(j) * static_cast<size_t>(fftSize),
                                     zAcc.data(), scale);
        }
        fft->inverseZ(zAcc.data(), timeScratch.data());
        std::memcpy(tailBuf.data(), timeScratch.data() + partitionLength,
                    sizeof(float) * static_cast<size_t>(partitionLength));
    }
};

PartitionedConvolver::PartitionedConvolver() : impl_(std::make_unique<Impl>()) {}
PartitionedConvolver::~PartitionedConvolver() = default;
PartitionedConvolver::PartitionedConvolver(PartitionedConvolver&&) noexcept = default;
PartitionedConvolver& PartitionedConvolver::operator=(PartitionedConvolver&&) noexcept = default;

void PartitionedConvolver::prepare(const Config& config) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    Impl& s = *impl_;
    const int partLen = std::max(1, config.partitionLength);
    s.partitionLength = partLen;
    s.headLength = normalizeHead(std::max(1, config.headLength), partLen);
    assert(s.headLength == config.headLength && "headLength should be a multiple of partitionLength");
    s.fftSize = 2 * partLen;
    s.histLen = s.headLength - 1;
    s.roundedHead = roundUpTo(s.headLength, 8);
    const int maxTail = std::max(0, config.maxKernelLength - s.headLength);
    s.maxPartitions = (maxTail + partLen - 1) / partLen;
    s.qOffset = s.headLength / partLen;
    s.ringSize = std::max(1, s.maxPartitions + s.qOffset - 1);
    s.fft.emplace(s.fftSize); // throws on illegal size; prepare() is not RT
    s.hist.assign(static_cast<size_t>(s.histLen + partLen + 8), 0.0f);
    s.timeSeg.assign(static_cast<size_t>(s.fftSize), 0.0f);
    s.fdl.assign(static_cast<size_t>(s.ringSize) * static_cast<size_t>(s.fftSize), 0.0f);
    s.tailBuf.assign(static_cast<size_t>(partLen), 0.0f);
    s.zAcc.assign(static_cast<size_t>(s.fftSize), 0.0f);
    s.timeScratch.assign(static_cast<size_t>(s.fftSize), 0.0f);
    s.analyzeScratch.assign(static_cast<size_t>(s.fftSize), 0.0f);
    s.fifoPos = 0;
    s.fdlHead = 0;
    s.tailValid = false;
    s.tailRevision = 0;
    s.prepared = true;
}

void PartitionedConvolver::analyze(const Kernel& kernel, KernelImage& out) const noexcept {
    if (!impl_ || !impl_->prepared || !out.impl_ || !out.impl_->prepared) {
        assert(false && "analyze() before prepare()");
        return;
    }
    const Impl& s = *impl_;
    KernelImage::Impl& img = *out.impl_;
    if (img.headLength != s.headLength || img.partitionLength != s.partitionLength) {
        assert(false && "KernelImage prepared with a different layout");
        return;
    }
    const float* h = kernel.data();
    int len = std::max(0, kernel.length());
    const int cap = img.headLength + img.maxPartitions * img.partitionLength;
    if (len > cap) {
        assert(false && "kernel longer than KernelImage capacity");
        len = cap;
    }
    // Head taps, reversed so process() runs a forward dot product; zero-pad to roundedHead.
    for (int u = 0; u < img.roundedHead; ++u) {
        const int t = s.headLength - 1 - u;
        img.headRev[static_cast<size_t>(u)] = (t >= 0 && t < len) ? h[t] : 0.0f;
    }
    // Tail partitions: zero-padded 2*partitionLength forward FFTs (z-domain).
    const int tail = len - s.headLength;
    img.numPartitions = tail > 0 ? (tail + s.partitionLength - 1) / s.partitionLength : 0;
    float* scratch = s.analyzeScratch.data();
    for (int p = 0; p < img.numPartitions; ++p) {
        const int base = s.headLength + p * s.partitionLength;
        const int m = std::min(s.partitionLength, len - base);
        std::memcpy(scratch, h + base, sizeof(float) * static_cast<size_t>(m));
        std::memset(scratch + m, 0, sizeof(float) * static_cast<size_t>(s.fftSize - m));
        s.fft->forwardZ(scratch,
                        img.spectra.data() + static_cast<size_t>(p) * static_cast<size_t>(s.fftSize));
    }
    img.kernelLength = len;
    img.latency = kernel.latencySamples();
    img.revision = 1 + gImageRevision.fetch_add(1, std::memory_order_relaxed);
}

void PartitionedConvolver::reset() noexcept {
    if (!impl_ || !impl_->prepared)
        return;
    Impl& s = *impl_;
    std::memset(s.hist.data(), 0, sizeof(float) * s.hist.size());
    std::memset(s.timeSeg.data(), 0, sizeof(float) * s.timeSeg.size());
    std::memset(s.fdl.data(), 0, sizeof(float) * s.fdl.size());
    std::memset(s.tailBuf.data(), 0, sizeof(float) * s.tailBuf.size());
    s.fifoPos = 0;
    s.fdlHead = 0;
    s.tailValid = false;
    s.tailRevision = 0;
}

void PartitionedConvolver::copyStateFrom(const PartitionedConvolver& other) noexcept {
    if (this == &other)
        return;
    if (!impl_ || !other.impl_) {
        assert(false);
        return;
    }
    Impl& d = *impl_;
    const Impl& o = *other.impl_;
    if (!d.prepared || !o.prepared || d.headLength != o.headLength
        || d.partitionLength != o.partitionLength || d.maxPartitions != o.maxPartitions
        || d.ringSize != o.ringSize) {
        assert(false && "copyStateFrom() requires identically prepared convolvers");
        return;
    }
    std::memcpy(d.hist.data(), o.hist.data(), sizeof(float) * d.hist.size());
    std::memcpy(d.timeSeg.data(), o.timeSeg.data(), sizeof(float) * d.timeSeg.size());
    std::memcpy(d.fdl.data(), o.fdl.data(), sizeof(float) * d.fdl.size());
    std::memcpy(d.tailBuf.data(), o.tailBuf.data(), sizeof(float) * d.tailBuf.size());
    d.fifoPos = o.fifoPos;
    d.fdlHead = o.fdlHead;
    d.tailValid = o.tailValid;
    d.tailRevision = o.tailRevision;
}

void PartitionedConvolver::process(const KernelImage& kernel, const float* in, float* out,
                                   int n) noexcept {
    if (n <= 0)
        return;
    if (!impl_ || !impl_->prepared || !kernel.impl_ || !kernel.impl_->prepared) {
        assert(false && "process() before prepare()");
        std::memset(out, 0, sizeof(float) * static_cast<size_t>(n));
        return;
    }
    Impl& s = *impl_;
    const KernelImage::Impl& img = *kernel.impl_;
    if (img.headLength != s.headLength || img.partitionLength != s.partitionLength) {
        assert(false && "KernelImage layout mismatch");
        std::memset(out, 0, sizeof(float) * static_cast<size_t>(n));
        return;
    }
    // Kernel changed since the cached tail was computed (fresh instance, copyStateFrom into a
    // crossfade partner, reset, ...): rebuild the current block's tail from the FDL.
    if (!s.tailValid || s.tailRevision != img.revision) {
        s.computeTail(img);
        s.tailValid = true;
        s.tailRevision = img.revision;
    }
    const float* __restrict hrev = img.headRev.data();
    const int rounded = s.roundedHead;
    float* hist = s.hist.data();
    int pos = 0;
    while (pos < n) {
        const int chunk = std::min(n - pos, s.partitionLength - s.fifoPos);
        const float* x = in + pos;
        float* y = out + pos;
        // Stage the inputs first: makes in == out (in-place) safe, and gives the head a
        // fully contiguous window per output sample.
        std::memcpy(hist + s.histLen, x, sizeof(float) * static_cast<size_t>(chunk));
        std::memcpy(s.timeSeg.data() + s.partitionLength + s.fifoPos, x,
                    sizeof(float) * static_cast<size_t>(chunk));
        const float* tb = s.tailBuf.data() + s.fifoPos;
        for (int i = 0; i < chunk; ++i)
            y[i] = dot8(hrev, hist + i, rounded) + tb[i];
        // Keep the last histLen samples as the next chunk's past window.
        std::memmove(hist, hist + chunk, sizeof(float) * static_cast<size_t>(s.histLen));
        s.fifoPos += chunk;
        pos += chunk;
        if (s.fifoPos == s.partitionLength) {
            // Input block completed: push its spectrum into the FDL and precompute the tail
            // contribution for the NEXT output block (Gardner: only completed blocks used).
            s.fdlHead = (s.fdlHead + 1 == s.ringSize) ? 0 : s.fdlHead + 1;
            s.fft->forwardZ(s.timeSeg.data(),
                            s.fdl.data() + static_cast<size_t>(s.fdlHead) * static_cast<size_t>(s.fftSize));
            s.computeTail(img);
            s.tailRevision = img.revision; // tailValid already true
            std::memcpy(s.timeSeg.data(), s.timeSeg.data() + s.partitionLength,
                        sizeof(float) * static_cast<size_t>(s.partitionLength));
            s.fifoPos = 0;
        }
    }
}

// ============================ ConvolutionSection =============================

struct ConvolutionSection::Impl {
    PartitionedConvolver::Config cfg;
    bool prepared = false;
    int numChannels = 0;
    int fadeLength = 1;
    std::vector<PartitionedConvolver> convs; // [channel * 2 + instance]
    KernelImage images[3];                   // rotating roles below
    int activeImg = 0;
    int incomingImg = 1;
    int spareImg = 2;
    int activeInst = 0; // which of the two per-channel instances is live
    bool fading = false;
    int fadePos = 0;
    AlignedVector<float> scratch; // outgoing-instance output, one channel at a time
};

ConvolutionSection::ConvolutionSection() : impl_(std::make_unique<Impl>()) {}
ConvolutionSection::~ConvolutionSection() = default;

void ConvolutionSection::prepare(const PartitionedConvolver::Config& config, int numChannels,
                                 int fadeLengthSamples) {
    if (!impl_)
        impl_ = std::make_unique<Impl>();
    Impl& s = *impl_;
    s.cfg = config;
    s.numChannels = std::max(1, numChannels);
    s.fadeLength = std::max(1, fadeLengthSamples);
    s.convs.clear();
    s.convs.resize(static_cast<size_t>(s.numChannels) * 2);
    for (auto& c : s.convs)
        c.prepare(config);
    for (auto& im : s.images)
        im.prepare(config.maxKernelLength, config.headLength, config.partitionLength);
    s.scratch.assign(static_cast<size_t>(std::max(1, config.maxBlockSize)), 0.0f);
    s.activeImg = 0;
    s.incomingImg = 1;
    s.spareImg = 2;
    s.activeInst = 0;
    s.fading = false;
    s.fadePos = 0;
    s.prepared = true;
}

void ConvolutionSection::reset() noexcept {
    if (!impl_ || !impl_->prepared)
        return;
    Impl& s = *impl_;
    for (auto& c : s.convs)
        c.reset();
    s.fading = false;
    s.fadePos = 0;
    // Kernel images are kept: reset() clears the audio stream, not the loaded kernel.
}

bool ConvolutionSection::pushKernel(const Kernel& kernel) noexcept {
    if (!impl_ || !impl_->prepared) {
        assert(false);
        return false;
    }
    Impl& s = *impl_;
    if (s.fading)
        return false; // caller retries at the next kernel tick
    s.convs[0].analyze(kernel, s.images[static_cast<size_t>(s.spareImg)]);
    std::swap(s.incomingImg, s.spareImg);
    // The idle instance inherits the live stream so both convolve the same history.
    for (int ch = 0; ch < s.numChannels; ++ch) {
        auto& live = s.convs[static_cast<size_t>(ch * 2 + s.activeInst)];
        auto& idle = s.convs[static_cast<size_t>(ch * 2 + (1 - s.activeInst))];
        idle.copyStateFrom(live);
    }
    s.fading = true;
    s.fadePos = 0;
    return true;
}

void ConvolutionSection::setKernelImmediate(const Kernel& kernel) noexcept {
    if (!impl_ || !impl_->prepared) {
        assert(false);
        return;
    }
    Impl& s = *impl_;
    s.convs[0].analyze(kernel, s.images[static_cast<size_t>(s.spareImg)]);
    std::swap(s.activeImg, s.spareImg);
    s.fading = false;
    s.fadePos = 0;
    for (auto& c : s.convs)
        c.reset(); // caller masks the discontinuity with its own fade (mode switches)
}

bool ConvolutionSection::isFading() const noexcept { return impl_ && impl_->fading; }

void ConvolutionSection::process(float* const* channels, int n) noexcept {
    if (!impl_ || !impl_->prepared || channels == nullptr || n <= 0)
        return;
    Impl& s = *impl_;
    int pos = 0;
    while (pos < n) {
        if (s.fading) {
            int chunk = std::min(n - pos, s.fadeLength - s.fadePos);
            chunk = std::min(chunk, static_cast<int>(s.scratch.size()));
            const float invFade = 1.0f / static_cast<float>(s.fadeLength);
            for (int ch = 0; ch < s.numChannels; ++ch) {
                float* data = channels[ch] + pos;
                auto& outgoing = s.convs[static_cast<size_t>(ch * 2 + s.activeInst)];
                auto& incoming = s.convs[static_cast<size_t>(ch * 2 + (1 - s.activeInst))];
                outgoing.process(s.images[static_cast<size_t>(s.activeImg)], data,
                                 s.scratch.data(), chunk);
                incoming.process(s.images[static_cast<size_t>(s.incomingImg)], data, data,
                                 chunk); // in-place safe
                const float* a = s.scratch.data();
                for (int i = 0; i < chunk; ++i) {
                    const float t = static_cast<float>(s.fadePos + i + 1) * invFade;
                    data[i] = a[i] + t * (data[i] - a[i]); // (1-t)*A + t*B, linear
                }
            }
            s.fadePos += chunk;
            pos += chunk;
            if (s.fadePos >= s.fadeLength) {
                // Fade complete: the incoming instance/image become active; the outgoing
                // instance idles (steady-state cost is one instance per channel).
                s.fading = false;
                s.fadePos = 0;
                s.activeInst = 1 - s.activeInst;
                std::swap(s.activeImg, s.incomingImg);
            }
        } else {
            const int chunk = n - pos;
            for (int ch = 0; ch < s.numChannels; ++ch) {
                float* data = channels[ch] + pos;
                s.convs[static_cast<size_t>(ch * 2 + s.activeInst)].process(
                    s.images[static_cast<size_t>(s.activeImg)], data, data, chunk);
            }
            pos += chunk;
        }
    }
}

int ConvolutionSection::currentLatencySamples() const noexcept {
    if (!impl_ || !impl_->prepared)
        return 0;
    return impl_->images[static_cast<size_t>(impl_->activeImg)].latencySamples();
}

} // namespace ftc
