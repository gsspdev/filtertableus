// Engine acceptance: NaN/inf recovery, denormal-free silence decay, live wavetable swaps.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "ftc/FilterTableEngine.h"
#include "helpers/TestSignals.h"
#include "test_engine_utils.h"

using ftc::PhaseMode;
namespace fe = ftet;

TEST_CASE("NaN/inf injection: output is finite again within one block and stays finite",
          "[engine][robust]") {
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(),
                         std::numeric_limits<float>::infinity()};
    for (float poison : bad) {
        fe::Harness h;
        h.params.mode = PhaseMode::Linear; // exercises both the wet history and the dry delay
        h.params.cutoffHz = 2000.0f;
        h.params.mix = 0.5f;
        h.engine.setWavetable(fe::morphTable());
        h.prepare();

        auto input = fe::makeNoise(16384, 21u, 0.4f);
        input[5 * 512 + 100] = poison; // one poisoned sample in block 5

        std::vector<std::vector<float>> bufs{input, input};
        for (int start = 0; start < 16384; start += 512) {
            h.block(bufs, static_cast<size_t>(start), 512);
            // Finite after every block, including the one carrying the poison.
            for (int i = start; i < start + 512; ++i)
                REQUIRE(std::isfinite(bufs[0][static_cast<size_t>(i)]));
        }
        // The engine keeps processing audio afterwards (it did not latch into silence).
        const auto tail = std::span<const float>(bufs[0]).subspan(8192);
        CHECK(fe::rms(tail) > 1e-4);
    }
}

TEST_CASE("denormal safety: impulse then silence decays to exact zeros, no subnormals",
          "[engine][robust]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 800.0f;
    h.params.resonance = 0.6f;
    h.params.mix = 0.7f;
    h.engine.setWavetable(fe::morphTable());
    h.prepare();

    constexpr int kN = 96000; // 2 s
    std::vector<float> sig(static_cast<size_t>(kN), 0.0f);
    sig[0] = 1.0f;
    const auto out = h.renderMono(sig);

    for (size_t i = 0; i < out.size(); ++i) {
        REQUIRE(std::isfinite(out[i]));
        REQUIRE(std::fpclassify(out[i]) != FP_SUBNORMAL);
    }
    // Kernel support is L and the dry delay at most L/2: by 2L everything must be EXACTLY 0
    // (the silence-idle path guarantees cheap exact zeros from ~L + one kernel tick onward).
    for (size_t i = 2 * fe::kL; i < out.size(); ++i)
        REQUIRE(out[i] == 0.0f);
    // And it wakes up again when signal returns.
    const auto again = h.renderMono(fe::makeNoise(4096, 77u, 0.4f));
    CHECK(fe::rms(std::span<const float>(again).subspan(1024)) > 1e-4);
    CHECK(fe::allFinite(again));
}

TEST_CASE("wavetable swap mid-render crossfades without clicks and updates the UI mirror",
          "[engine][robust]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 1500.0f;
    h.params.mix = 1.0f;
    auto t1 = fe::flatTable();
    auto t2 = fe::morphTable();
    h.engine.setWavetable(t1);
    h.prepare();

    constexpr int kN = 32768;
    const auto input = fe::makeNoise(kN, 33u, 0.4f);
    std::vector<std::vector<float>> bufs{input, input};
    for (int start = 0; start < kN; start += 512) {
        if (start == 16384)
            h.engine.setWavetable(t2); // message-thread call between blocks
        h.block(bufs, static_cast<size_t>(start), 512);
    }
    REQUIRE(fe::allFinite(bufs[0]));
    CHECK(h.engine.currentTableForUi() == t2);

    // The swap region must stay smooth (kernel change rides the normal crossfade).
    const float swapScore =
        ftt::clickScore(std::span<const float>(bufs[0]).subspan(16384 - 1024, 8192));
    const float steadyScore =
        ftt::clickScore(std::span<const float>(bufs[0]).subspan(4096, 8192));
    INFO("swap " << swapScore << " steady " << steadyScore);
    CHECK(swapScore <= 4.0f * steadyScore);

    // A fresh response curve is published for the new table.
    ftc::ResponseCurve rc;
    CHECK(h.engine.readResponseCurve(rc));

    h.engine.collectGarbage(); // t1 may be retired; must not disturb the stream
    const auto more = h.renderMono(fe::makeNoise(4096, 34u, 0.4f));
    CHECK(fe::allFinite(more));
}
