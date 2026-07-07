// A1 acceptance tests: ConvolutionSection A/B crossfading multi-channel wrapper.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <span>
#include <vector>

#include "ftc/Convolver.h"
#include "ftc/EngineConfig.h"
#include "ftc/Kernel.h"
#include "helpers/TestSignals.h"
#include "test_convolver_utils.h"

using ftc::ConvolutionSection;
using ftc::Kernel;
using ftc::KernelImage;
using ftc::PartitionedConvolver;

namespace {

PartitionedConvolver::Config defaultConfig() {
    PartitionedConvolver::Config cfg;
    cfg.maxKernelLength = 2048;
    cfg.headLength = ftc::EngineConfig::headLength;
    cfg.partitionLength = ftc::EngineConfig::partitionLength;
    cfg.maxBlockSize = 8192;
    return cfg;
}

constexpr int kFadeLen = 128; // one kernel tick at 48 kHz (EngineConfig::kernelUpdateInterval)

/// Feed section in place over bufs[ch][pos, end) with a repeating block-size sequence.
void feedSection(ConvolutionSection& s, std::vector<std::vector<float>>& bufs, size_t& pos,
                 size_t end, std::span<const int> sizes) {
    size_t si = 0;
    while (pos < end) {
        int n = sizes[si % sizes.size()];
        ++si;
        n = std::min<int>(n, static_cast<int>(end - pos));
        float* chans[8] = {};
        for (size_t c = 0; c < bufs.size(); ++c)
            chans[c] = bufs[c].data() + pos;
        s.process(chans, n);
        pos += static_cast<size_t>(n);
    }
}

std::vector<float> sineMix(int n, double fs) {
    std::vector<float> v(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        const double t = static_cast<double>(i) / fs;
        v[static_cast<size_t>(i)] =
            static_cast<float>(0.4 * std::sin(2.0 * 3.14159265358979 * 220.0 * t)
                               + 0.3 * std::sin(2.0 * 3.14159265358979 * 997.0 * t)
                               + 0.2 * std::sin(2.0 * 3.14159265358979 * 2500.0 * t));
    }
    return v;
}

} // namespace

TEST_CASE("crossfade equals (1-t)*convA + t*convB and settles bit-identically on B",
          "[convolver][section]") {
    const auto cfg = defaultConfig();
    const int total = 8192;
    const size_t pushAt = 4037; // deliberately not a partition boundary (4037 % 128 == 69)
    const size_t fadeEnd = pushAt + kFadeLen;

    const auto kernelA = ftct::randomKernel(2048, 111u, 0.05f);
    const auto kernelB = ftct::randomKernel(2048, 222u, 0.05f);
    const auto kernelC = ftct::randomKernel(2048, 333u, 0.05f);

    // Per-channel inputs differ to catch cross-channel state mixups.
    std::vector<std::vector<float>> x = {ftct::randomSignal(total, 1000u),
                                         ftct::randomSignal(total, 2000u)};
    std::vector<std::vector<float>> refA, refB;
    for (int ch = 0; ch < 2; ++ch) {
        refA.push_back(ftt::naiveConvolve(x[static_cast<size_t>(ch)], kernelA.taps()));
        refB.push_back(ftt::naiveConvolve(x[static_cast<size_t>(ch)], kernelB.taps()));
    }
    const float scale = std::max(
        std::max(ftct::maxAbs({refA[0].data(), static_cast<size_t>(total)}),
                 ftct::maxAbs({refA[1].data(), static_cast<size_t>(total)})),
        std::max(ftct::maxAbs({refB[0].data(), static_cast<size_t>(total)}),
                 ftct::maxAbs({refB[1].data(), static_cast<size_t>(total)})));
    const float tol = 1e-5f * scale;

    ConvolutionSection section;
    section.prepare(cfg, 2, kFadeLen);
    section.setKernelImmediate(kernelA);
    REQUIRE_FALSE(section.isFading());

    std::vector<std::vector<float>> y = x; // processed in place
    size_t pos = 0;
    const std::vector<int> mixed = {64, 441, 17, 128, 3, 1};
    feedSection(section, y, pos, pushAt, mixed);

    // Steady A region.
    for (int ch = 0; ch < 2; ++ch) {
        INFO("steady-A channel " << ch);
        REQUIRE(ftct::maxAbsDiff(y[static_cast<size_t>(ch)], refA[static_cast<size_t>(ch)], 0,
                                 pushAt)
                <= tol);
    }

    REQUIRE(section.pushKernel(kernelB));
    REQUIRE(section.isFading());
    REQUIRE_FALSE(section.pushKernel(kernelC)); // rejected while a fade is in flight

    // Drive across the fade with awkward chunks, including one call spanning the fade end.
    const std::vector<int> fadeChunks = {5, 1, 31, 7};
    feedSection(section, y, pos, pushAt + 44, fadeChunks);
    REQUIRE(section.isFading());
    const std::vector<int> spanChunk = {300}; // 84 fading + 216 steady-B samples
    feedSection(section, y, pos, pushAt + 344, spanChunk);
    REQUIRE_FALSE(section.isFading());
    feedSection(section, y, pos, static_cast<size_t>(total), mixed);

    // During the fade: exact linear kernel-space interpolation (by convolution linearity).
    for (int ch = 0; ch < 2; ++ch) {
        float err = 0.0f;
        for (int i = 0; i < kFadeLen; ++i) {
            const float t = static_cast<float>(i + 1) / static_cast<float>(kFadeLen);
            const size_t n = pushAt + static_cast<size_t>(i);
            const float a = refA[static_cast<size_t>(ch)][n];
            const float b = refB[static_cast<size_t>(ch)][n];
            const float want = a + t * (b - a);
            err = std::max(err, std::fabs(y[static_cast<size_t>(ch)][n] - want));
        }
        INFO("fade region channel " << ch << ", err " << err << ", tol " << tol);
        REQUIRE(err <= tol);
    }

    // After the fade: matches naive B within tolerance...
    for (int ch = 0; ch < 2; ++ch) {
        INFO("steady-B channel " << ch);
        REQUIRE(ftct::maxAbsDiff(y[static_cast<size_t>(ch)], refB[static_cast<size_t>(ch)],
                                 fadeEnd, static_cast<size_t>(total))
                <= tol);
    }

    // ... and is BIT-identical to an uninterrupted B-only convolver over the same stream
    // (the incoming instance inherited the full input history via copyStateFrom).
    for (int ch = 0; ch < 2; ++ch) {
        PartitionedConvolver mono;
        mono.prepare(cfg);
        KernelImage imgB;
        imgB.prepare(cfg.maxKernelLength, cfg.headLength, cfg.partitionLength);
        mono.analyze(kernelB, imgB);
        const auto yB = ftct::runMono(mono, imgB, x[static_cast<size_t>(ch)], mixed);
        INFO("post-fade bit-exactness channel " << ch);
        REQUIRE(ftct::maxAbsDiff(y[static_cast<size_t>(ch)], yB, fadeEnd,
                                 static_cast<size_t>(total))
                == 0.0f);
    }

    // A new push is accepted once the fade has completed.
    REQUIRE(section.pushKernel(kernelC));
}

TEST_CASE("first pushKernel fades in from silence", "[convolver][section]") {
    const auto cfg = defaultConfig();
    const int total = 4096;
    const auto x = ftct::randomSignal(total, 42u);
    const auto kernelA = ftct::randomKernel(2048, 7u, 0.05f);
    const auto refA = ftt::naiveConvolve(x, kernelA.taps());
    const float tol = 1e-5f * ftct::maxAbs({refA.data(), static_cast<size_t>(total)});

    ConvolutionSection section;
    section.prepare(cfg, 1, kFadeLen);
    REQUIRE(section.pushKernel(kernelA)); // no kernel loaded yet: fades from silence
    std::vector<std::vector<float>> y = {x};
    size_t pos = 0;
    const std::vector<int> mixed = {17, 3, 190, 1};
    feedSection(section, y, pos, static_cast<size_t>(total), mixed);

    float err = 0.0f;
    for (int i = 0; i < total; ++i) {
        const float t =
            i < kFadeLen ? static_cast<float>(i + 1) / static_cast<float>(kFadeLen) : 1.0f;
        err = std::max(err, std::fabs(y[0][static_cast<size_t>(i)]
                                      - t * refA[static_cast<size_t>(i)]));
    }
    REQUIRE(err <= tol);
}

TEST_CASE("crossfade produces no clicks (second-difference detector)", "[convolver][section]") {
    const auto cfg = defaultConfig();
    const int total = 8192;
    const size_t pushAt = 4096;
    auto kernelA = ftct::randomKernel(2048, 51u);
    auto kernelB = ftct::randomKernel(2048, 52u);
    ftct::normalizeKernel(kernelA);
    ftct::normalizeKernel(kernelB);

    std::vector<std::vector<float>> y = {sineMix(total, 48000.0)};
    ConvolutionSection section;
    section.prepare(cfg, 1, kFadeLen);
    section.setKernelImmediate(kernelA);

    size_t pos = 0;
    const std::vector<int> blocks = {64};
    feedSection(section, y, pos, pushAt, blocks);
    REQUIRE(section.pushKernel(kernelB));
    feedSection(section, y, pos, static_cast<size_t>(total), blocks);

    const int win = 2176; // fade (128) + 1024 samples either side
    const auto& out = y[0];
    const float steadyScore =
        ftt::clickScore({out.data() + 1024, static_cast<size_t>(win)});
    const float fadeScore =
        ftt::clickScore({out.data() + pushAt - 1024, static_cast<size_t>(win)});
    INFO("steady " << steadyScore << ", fade " << fadeScore);
    REQUIRE(fadeScore <= steadyScore * 2.0f + 0.01f);

    // Detector sanity: an injected discontinuity in the same window must score far higher.
    std::vector<float> corrupted(out.begin() + static_cast<long>(pushAt) - 1024,
                                 out.begin() + static_cast<long>(pushAt) - 1024 + win);
    corrupted[1024 + 64] += 1.0f;
    const float corruptedScore = ftt::clickScore(corrupted);
    REQUIRE(corruptedScore > fadeScore * 2.0f);
    REQUIRE(corruptedScore > steadyScore * 2.0f);
}

TEST_CASE("setKernelImmediate hard-swaps and resets stream state", "[convolver][section]") {
    const auto cfg = defaultConfig();
    const int total = 6000;
    const int swapAt = 2000;
    const auto x = ftct::randomSignal(total, 61u);
    auto kernelA = ftct::randomKernel(2048, 62u, 0.05f);
    auto kernelB = ftct::randomKernel(2048, 63u, 0.05f);
    kernelB.setLatency(1024);

    ConvolutionSection section;
    section.prepare(cfg, 1, kFadeLen);
    section.setKernelImmediate(kernelA);
    REQUIRE(section.currentLatencySamples() == 0);

    std::vector<std::vector<float>> y = {x};
    size_t pos = 0;
    const std::vector<int> mixed = {128, 33, 1, 441};
    feedSection(section, y, pos, static_cast<size_t>(swapAt), mixed);

    section.setKernelImmediate(kernelB);
    REQUIRE(section.currentLatencySamples() == 1024); // reported immediately, no fade
    REQUIRE_FALSE(section.isFading());
    feedSection(section, y, pos, static_cast<size_t>(total), mixed);

    // Post-swap output = fresh convolution of ONLY the post-swap input with B
    // (history was reset; the engine masks the click with its own wet fade).
    const std::span<const float> xTail{x.data() + swapAt, static_cast<size_t>(total - swapAt)};
    const auto refTail = ftt::naiveConvolve(xTail, kernelB.taps());
    const float tol = 1e-5f * ftct::maxAbs({refTail.data(), xTail.size()});
    REQUIRE(ftct::maxAbsDiff({y[0].data() + swapAt, xTail.size()}, refTail, 0, xTail.size())
            <= tol);
}

TEST_CASE("currentLatencySamples reports the ACTIVE image during a fade",
          "[convolver][section]") {
    const auto cfg = defaultConfig();
    auto kernelA = ftct::randomKernel(2048, 71u, 0.05f);
    auto kernelB = ftct::randomKernel(2048, 72u, 0.05f);
    kernelB.setLatency(512);

    ConvolutionSection section;
    section.prepare(cfg, 1, kFadeLen);
    section.setKernelImmediate(kernelA);

    std::vector<std::vector<float>> y = {ftct::randomSignal(1024, 73u)};
    size_t pos = 0;
    const std::vector<int> blocks = {64};
    feedSection(section, y, pos, 512, blocks);

    REQUIRE(section.pushKernel(kernelB));
    REQUIRE(section.currentLatencySamples() == 0); // outgoing image still active mid-fade
    feedSection(section, y, pos, 512 + kFadeLen, blocks);
    REQUIRE_FALSE(section.isFading());
    REQUIRE(section.currentLatencySamples() == 512); // incoming image took over
}

TEST_CASE("section reset() replays bit-identically while keeping the kernel",
          "[convolver][section]") {
    const auto cfg = defaultConfig();
    const auto x = ftct::randomSignal(3000, 81u);
    const auto kernelA = ftct::randomKernel(2048, 82u, 0.05f);

    ConvolutionSection used;
    used.prepare(cfg, 1, kFadeLen);
    used.setKernelImmediate(kernelA);
    std::vector<std::vector<float>> garbage = {ftct::randomSignal(2777, 83u)};
    size_t gpos = 0;
    const std::vector<int> mixed = {17, 3, 190, 1};
    feedSection(used, garbage, gpos, garbage[0].size(), mixed);
    used.reset();

    ConvolutionSection fresh;
    fresh.prepare(cfg, 1, kFadeLen);
    fresh.setKernelImmediate(kernelA);

    std::vector<std::vector<float>> yUsed = {x}, yFresh = {x};
    size_t p1 = 0, p2 = 0;
    feedSection(used, yUsed, p1, x.size(), mixed);
    feedSection(fresh, yFresh, p2, x.size(), mixed);
    REQUIRE(ftct::maxAbsDiff(yUsed[0], yFresh[0]) == 0.0f);
}
