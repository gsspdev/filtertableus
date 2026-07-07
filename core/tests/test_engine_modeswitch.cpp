// Engine acceptance: mid-render phase-mode switches — immediate latency report, no NaN, and
// no discontinuity beyond the 5 ms wet fade envelope's own shape.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/EngineConfig.h"
#include "ftc/FilterTableEngine.h"
#include "helpers/TestSignals.h"
#include "test_engine_utils.h"

using ftc::PhaseMode;
namespace fe = ftet;

namespace {

constexpr int kN = 49152;
constexpr int kSwitchAt = 24576;              // block-aligned -> the request tick is pos 0
constexpr int kFade = 256;                    // 5 ms at 48 kHz rounded up to control ticks
constexpr int kHold = 1024;                   // new-mode latency: wet pipeline fill time

/// The mode-switch wet envelope the engine applies: 5 ms fade-out, hard swap at zero, hold at
/// zero while the reset convolver's pipeline fills (the new mode's latency), 5 ms fade-in —
/// starting at the first sample of the block that carries the new mode.
float fadeEnvelope(int i) {
    if (i < kSwitchAt)
        return 1.0f;
    const int k = i - kSwitchAt;
    if (k < kFade)
        return static_cast<float>(kFade - 1 - k) / static_cast<float>(kFade);
    if (k < kFade + kHold)
        return 0.0f;
    if (k < 2 * kFade + kHold)
        return static_cast<float>(k - kFade - kHold + 1) / static_cast<float>(kFade);
    return 1.0f;
}

} // namespace

TEST_CASE("mode switch mid-render: immediate latency, finite output, fade-shaped only",
          "[engine][modeswitch]") {
    const auto sine = fe::makeSine(kN, 220.0, 0.5f);

    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 20000.0f;
    h.params.mix = 1.0f;
    h.engine.setWavetable(fe::flatTable());
    h.prepare();
    REQUIRE(h.engine.latencySamples() == 0);

    std::vector<std::vector<float>> bufs{sine, sine};
    for (int start = 0; start < kN; start += 512) {
        if (start == kSwitchAt)
            h.params.mode = PhaseMode::Linear;
        h.block(bufs, static_cast<size_t>(start), std::min(512, kN - start));
        if (start < kSwitchAt)
            CHECK(h.engine.latencySamples() == 0);
        else // reported from the very process() call that carried the new mode
            CHECK(h.engine.latencySamples() == fe::kL / 2);
    }
    const auto& out = bufs[0];
    REQUIRE(fe::allFinite(out));

    // Reference: the same sine shaped by the fade envelope alone. The output's local
    // second-difference statistics must not exceed the envelope's own shape (factor 4,
    // measured around the switch region). After the swap the wet is the L/2-delayed sine, so
    // the reference uses the delayed signal under the fade-in for a fair local-RMS profile.
    std::vector<float> ref(static_cast<size_t>(kN));
    for (int i = 0; i < kN; ++i) {
        const int src = i < kSwitchAt + kFade ? i : std::max(i - fe::kL / 2, 0);
        ref[static_cast<size_t>(i)] = sine[static_cast<size_t>(src)] * fadeEnvelope(i);
    }
    const auto region = [](const std::vector<float>& v) {
        return std::span<const float>(v).subspan(kSwitchAt - 2048,
                                                 2 * kFade + kHold + 4096);
    };
    const float outScore = ftt::clickScore(region(out));
    const float refScore = ftt::clickScore(region(ref));
    INFO("switch-region click score " << outScore << " vs fade-envelope reference "
                                      << refScore);
    CHECK(outScore <= 4.0f * refScore + 0.02f);

    // Away from the switch the output is a plain (near-passthrough) sine: no clicks at all
    // (absolute floor covers the Minimum kernel's documented ~-66 dB FastMath ripple).
    const float steadyScore =
        ftt::clickScore(std::span<const float>(out).subspan(4096, 16384));
    const float sineScore = ftt::clickScore(std::span<const float>(sine).subspan(4096, 16384));
    CHECK(steadyScore <= std::max(4.0f * sineScore, 0.02f));
}

TEST_CASE("mode switch cycles through all modes without blowing up", "[engine][modeswitch]") {
    const auto noise = fe::makeNoise(kN, 5150u, 0.4f);
    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 2000.0f;
    h.params.mix = 0.6f;
    h.engine.setWavetable(fe::morphTable());
    h.prepare();

    const PhaseMode order[] = {PhaseMode::Linear, PhaseMode::Original, PhaseMode::Raw,
                               PhaseMode::Minimum};
    std::vector<std::vector<float>> bufs{noise, noise};
    int m = 0;
    for (int start = 0; start < kN; start += 512) {
        if (start > 0 && start % 8192 == 0)
            h.params.mode = order[static_cast<size_t>(m++ % 4)];
        h.block(bufs, static_cast<size_t>(start), std::min(512, kN - start));
        CHECK(h.engine.latencySamples()
              == ftc::FilterTableEngine::latencySamplesFor(h.params.mode, fe::kFs));
    }
    CHECK(fe::allFinite(bufs[0]));
    CHECK(fe::allFinite(bufs[1]));
}

TEST_CASE("mode switch during silence idle: fresh state and exact new-mode latency on resume",
          "[engine][modeswitch]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 20000.0f;
    h.params.mix = 1.0f;
    h.prepare(); // no table -> exact single-tap kernels

    // Impulse, then enough silence to enter the idle path (> L + one kernel tick).
    std::vector<float> sig(static_cast<size_t>(16384), 0.0f);
    sig[0] = 1.0f;
    constexpr int kSecond = 8192; // second impulse well after idle entry (~2240)
    sig[kSecond] = 1.0f;

    std::vector<std::vector<float>> bufs{sig, sig};
    for (int start = 0; start < 16384; start += 512) {
        if (start == 4096)
            h.params.mode = PhaseMode::Linear; // switched while idle
        h.block(bufs, static_cast<size_t>(start), 512);
    }
    const auto& out = bufs[0];
    REQUIRE(fe::allFinite(out));
    CHECK(h.engine.latencySamples() == fe::kL / 2);

    // First impulse answered at latency 0 (Minimum), second at exactly L/2 (Linear).
    CHECK(std::abs(out[0]) > 0.9f);
    const auto tail = std::span<const float>(out).subspan(kSecond);
    CHECK(fe::argMaxAbs(tail) == fe::kL / 2);
    CHECK(std::abs(tail[static_cast<size_t>(fe::kL / 2)]) > 0.9f);
}
