// A1 acceptance tests: mono PartitionedConvolver correctness.
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <span>
#include <vector>

#include "ftc/Convolver.h"
#include "ftc/EngineConfig.h"
#include "ftc/Kernel.h"
#include "helpers/TestSignals.h"
#include "test_convolver_utils.h"

using ftc::Kernel;
using ftc::KernelImage;
using ftc::PartitionedConvolver;

namespace {

PartitionedConvolver::Config defaultConfig() {
    PartitionedConvolver::Config cfg;
    cfg.maxKernelLength = 2048;
    cfg.headLength = ftc::EngineConfig::headLength;           // 128
    cfg.partitionLength = ftc::EngineConfig::partitionLength; // 128
    cfg.maxBlockSize = 8192;
    return cfg;
}

const std::vector<std::vector<int>>& blockSequences() {
    static const std::vector<std::vector<int>> seqs = {
        {1}, {17}, {64}, {441}, {4096}, {1, 17, 64, 441, 3, 128}};
    return seqs;
}

} // namespace

TEST_CASE("PartitionedConvolver matches naive convolution across kernel lengths and block sizes",
          "[convolver]") {
    const auto cfg = defaultConfig();
    const int inputLen = 8192;
    const auto x = ftct::randomSignal(inputLen, 2026u);

    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);

    for (int L : {129, 1000, 2048}) {
        const auto kernel = ftct::randomKernel(L, 77u + static_cast<unsigned>(L));
        conv.analyze(kernel, img);
        REQUIRE(img.latencySamples() == 0);

        const auto ref = ftt::naiveConvolve(x, kernel.taps()); // length inputLen + L - 1
        const float tol = 1e-5f * ftct::maxAbs({ref.data(), static_cast<size_t>(inputLen)});

        for (const auto& seq : blockSequences()) {
            conv.reset();
            const auto y = ftct::runMono(conv, img, x, seq);
            const float err =
                ftct::maxAbsDiff(y, ref, 0, static_cast<size_t>(inputLen));
            INFO("kernel length " << L << ", first block size " << seq.front() << ", err " << err
                                  << ", tol " << tol);
            REQUIRE(err <= tol);
        }
    }
}

TEST_CASE("delta kernel h[0]=1 is an exact zero-latency identity", "[convolver]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(4096, 5u);
    const auto kernel = ftct::deltaKernel(2048, 0);

    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
    conv.analyze(kernel, img);

    const std::vector<int> mixed = {1, 17, 64, 441, 3, 128};
    const auto y = ftct::runMono(conv, img, x, mixed);
    // Zero-latency proof: output ≡ input at ZERO sample offset, bit-exact.
    REQUIRE(ftct::maxAbsDiff(y, x) == 0.0f);
}

TEST_CASE("delayed delta kernels shift the input by exactly k samples", "[convolver]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(4096, 6u);

    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);

    for (int k : {1, 127, 128, 500, 2047}) {
        const auto kernel = ftct::deltaKernel(2048, k);
        conv.analyze(kernel, img);
        conv.reset();
        const std::vector<int> sizes = {97, 3, 256};
        const auto y = ftct::runMono(conv, img, x, sizes);
        float err = 0.0f;
        for (size_t n = 0; n < y.size(); ++n) {
            const float want = n >= static_cast<size_t>(k) ? x[n - static_cast<size_t>(k)] : 0.0f;
            err = std::max(err, std::fabs(y[n] - want));
        }
        INFO("delay " << k << ", err " << err);
        // Head-path delays are exact; tail-path delays go through the 256-pt float FFT.
        REQUIRE(err <= (k < 128 ? 0.0f : 1e-5f));
    }
}

TEST_CASE("copyStateFrom continues the stream bit-identically", "[convolver]") {
    const auto cfg = defaultConfig();
    const int total = 6000;
    const int split = 777; // deliberately not a partition boundary
    const auto x = ftct::randomSignal(total, 99u);
    const auto kernel = ftct::randomKernel(1000, 1234u);

    PartitionedConvolver refConv;
    refConv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
    refConv.analyze(kernel, img);
    const std::vector<int> seqA = {1, 17, 64, 441, 3, 128};
    const auto ref = ftct::runMono(refConv, img, x, seqA);

    // First convolver handles [0, split) ...
    PartitionedConvolver c1;
    c1.prepare(cfg);
    std::vector<float> y(static_cast<size_t>(total), 0.0f);
    {
        size_t pos = 0, si = 0;
        while (pos < static_cast<size_t>(split)) {
            int n = seqA[si % seqA.size()];
            ++si;
            n = std::min<int>(n, split - static_cast<int>(pos));
            c1.process(img, x.data() + pos, y.data() + pos, n);
            pos += static_cast<size_t>(n);
        }
    }

    // ... a second, deliberately dirtied convolver takes over mid-stream.
    PartitionedConvolver c2;
    c2.prepare(cfg);
    const auto garbage = ftct::randomSignal(1000, 4321u);
    std::vector<float> sink(garbage.size());
    c2.process(img, garbage.data(), sink.data(), static_cast<int>(garbage.size()));

    c2.copyStateFrom(c1);
    {
        const std::vector<int> seqB = {5, 250, 1, 31}; // different chunking after the handoff
        size_t pos = static_cast<size_t>(split), si = 0;
        while (pos < static_cast<size_t>(total)) {
            int n = seqB[si % seqB.size()];
            ++si;
            n = std::min<int>(n, total - static_cast<int>(pos));
            c2.process(img, x.data() + pos, y.data() + pos, n);
            pos += static_cast<size_t>(n);
        }
    }

    REQUIRE(ftct::maxAbsDiff(y, ref) == 0.0f);
}

TEST_CASE("reset() restores the freshly-prepared state bit-identically", "[convolver]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(3000, 15u);
    const auto kernel = ftct::randomKernel(2048, 5150u);

    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);

    PartitionedConvolver used;
    used.prepare(cfg);
    used.analyze(kernel, img);
    const auto garbage = ftct::randomSignal(2500, 8u);
    std::vector<float> sink(garbage.size());
    used.process(img, garbage.data(), sink.data(), static_cast<int>(garbage.size()));
    used.reset();

    PartitionedConvolver fresh;
    fresh.prepare(cfg);

    const std::vector<int> sizes = {1, 17, 64, 441, 3, 128};
    const auto yUsed = ftct::runMono(used, img, x, sizes);
    const auto yFresh = ftct::runMono(fresh, img, x, sizes);
    REQUIRE(ftct::maxAbsDiff(yUsed, yFresh) == 0.0f);
}

TEST_CASE("in-place processing (in == out) matches out-of-place", "[convolver]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(2000, 21u);
    const auto kernel = ftct::randomKernel(2048, 22u);

    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
    PartitionedConvolver a;
    a.prepare(cfg);
    a.analyze(kernel, img);
    PartitionedConvolver b;
    b.prepare(cfg);

    const std::vector<int> sizes = {13, 200, 1, 64};
    const auto yOut = ftct::runMono(a, img, x, sizes);

    std::vector<float> inPlace = x;
    size_t pos = 0, si = 0;
    while (pos < inPlace.size()) {
        int n = sizes[si % sizes.size()];
        ++si;
        n = std::min<int>(n, static_cast<int>(inPlace.size() - pos));
        b.process(img, inPlace.data() + pos, inPlace.data() + pos, n);
        pos += static_cast<size_t>(n);
    }
    REQUIRE(ftct::maxAbsDiff(inPlace, yOut) == 0.0f);
}

TEST_CASE("KernelImage carries the source kernel's latency", "[convolver]") {
    const auto cfg = defaultConfig();
    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);

    auto kernel = ftct::randomKernel(2048, 3u);
    kernel.setLatency(1024);
    conv.analyze(kernel, img);
    REQUIRE(img.latencySamples() == 1024);

    kernel.setLatency(0);
    conv.analyze(kernel, img);
    REQUIRE(img.latencySamples() == 0);
}

TEST_CASE("kernels shorter than the head still convolve correctly", "[convolver]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(2048, 33u);
    const auto kernel = ftct::randomKernel(100, 34u); // head-only, no tail partitions

    PartitionedConvolver conv;
    conv.prepare(cfg);
    KernelImage img;
    img.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
    conv.analyze(kernel, img);

    const auto ref = ftt::naiveConvolve(x, kernel.taps());
    const std::vector<int> sizes = {1, 17, 64, 441, 3, 128};
    const auto y = ftct::runMono(conv, img, x, sizes);
    const float tol = 1e-5f * ftct::maxAbs({ref.data(), x.size()});
    REQUIRE(ftct::maxAbsDiff(y, ref, 0, x.size()) <= tol);
}
