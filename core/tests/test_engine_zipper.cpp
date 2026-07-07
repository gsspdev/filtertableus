// Engine acceptance: parameter sweeps produce no zipper — the click detector's max
// normalized second difference stays within 4x the static-parameter baseline.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/FilterTableEngine.h"
#include "helpers/TestSignals.h"
#include "test_engine_utils.h"

using ftc::PhaseMode;
namespace fe = ftet;

namespace {

constexpr int kN = 96000; // 2 s at 48 kHz
constexpr int kSettle = 4096;

enum class Sweep { None, Scan, Cutoff, Both };

// Sweeps run over a broadband morph table (both frames carry harmonics 1..512) so the output
// stays well above the click detector's RMS floor for the whole sweep — the score then
// measures parameter zipper, not FP noise normalized by near-silence. The cutoff sweep covers
// 100 Hz -> 10 kHz per the acceptance; with 512 harmonics the audible band stays populated.
std::vector<float> renderSweep(PhaseMode mode, Sweep sweep, const std::vector<float>& input) {
    fe::Harness h;
    h.params.mode = mode;
    h.params.scan = sweep == Sweep::Scan || sweep == Sweep::Both ? 0.0f : 0.5f;
    h.params.cutoffHz = sweep == Sweep::Cutoff || sweep == Sweep::Both ? 100.0f : 1000.0f;
    h.params.resonance = 0.2f;
    h.params.mix = 1.0f;
    h.engine.setWavetable(fe::fullBandMorphTable());
    h.prepare();

    std::vector<std::vector<float>> bufs{input, input};
    for (int start = 0; start < kN; start += 512) {
        const float t = static_cast<float>(start) / static_cast<float>(kN);
        if (sweep == Sweep::Scan || sweep == Sweep::Both)
            h.params.scan = t;
        if (sweep == Sweep::Cutoff || sweep == Sweep::Both)
            h.params.cutoffHz = 100.0f * std::pow(100.0f, t); // 100 Hz -> 10 kHz
        h.block(bufs, static_cast<size_t>(start), std::min(512, kN - start));
    }
    return bufs[0];
}

float scoreTail(const std::vector<float>& out) {
    return ftt::clickScore(std::span<const float>(out).subspan(kSettle));
}

} // namespace

TEST_CASE("zipper: scan and cutoff sweeps stay within 4x the static click baseline",
          "[engine][zipper]") {
    const auto input = fe::makeNoise(kN, 314159u, 0.5f);

    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original,
                           PhaseMode::Raw}) {
        const auto baselineOut = renderSweep(mode, Sweep::None, input);
        REQUIRE(fe::allFinite(baselineOut));
        const float baseline = scoreTail(baselineOut);
        REQUIRE(baseline > 0.0f);

        for (Sweep sweep : {Sweep::Scan, Sweep::Cutoff, Sweep::Both}) {
            const auto out = renderSweep(mode, sweep, input);
            REQUIRE(fe::allFinite(out));
            const float score = scoreTail(out);
            INFO("mode " << static_cast<int>(mode) << " sweep " << static_cast<int>(sweep)
                         << ": score " << score << " vs baseline " << baseline);
            CHECK(score <= 4.0f * baseline);
        }
    }
}
