// Kernel generation — scan morphing, robustness, passthrough and latency tests.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <vector>

#include "ftc/WavetableData.h"
#include "test_kernel_helpers.h"

using ftc::PhaseMode;

// Acceptance 8a: two-frame morph table at scan 0.5 -> response between the endpoints.
TEST_CASE("scan midpoint response lies between the endpoint responses", "[kernel][morph]") {
    auto morph = ftt::makeTwoFrameMorphTable(); // frame 0: h1..8, frame 1: h24..64
    auto table = ftc::WavetableData::analyze(morph, 2, "morph");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    const float fc = 4000.0f; // harmonic h at 166.7 h Hz
    fx.make(table.get(), PhaseMode::Linear, 0.0f, fc, 0.0f);
    auto resp0 = fx.response();
    fx.make(table.get(), PhaseMode::Linear, 1.0f, fc, 0.0f);
    auto resp1 = fx.response();
    fx.make(table.get(), PhaseMode::Linear, 0.5f, fc, 0.0f);
    auto respM = fx.response();

    for (double hz : {fc / 24.0 * 4.0, fc / 24.0 * 40.0}) { // dark-only and bright-only bins
        const float d0 = ftk::responseDbAt(resp0, fx.fs, fx.N, hz);
        const float d1 = ftk::responseDbAt(resp1, fx.fs, fx.N, hz);
        const float dm = ftk::responseDbAt(respM, fx.fs, fx.N, hz);
        INFO("hz=" << hz << " scan0=" << d0 << " scan1=" << d1 << " mid=" << dm);
        CHECK(dm >= std::min(d0, d1) - 1.5f);
        CHECK(dm <= std::max(d0, d1) + 1.5f);
    }
}

namespace {

/// Max adjacent-step |delta| of the peak-normalized linear response over a scan sweep.
float maxSweepJump(ftk::GenFixture& fx, const ftc::WavetableData* table, ftc::PhaseMode mode,
                   int steps) {
    std::vector<float> prev;
    float worst = 0.0f;
    for (int i = 0; i <= steps; ++i) {
        auto& k = fx.make(table, mode, static_cast<float>(i) / static_cast<float>(steps),
                          4000.0f, 0.0f);
        REQUIRE(ftk::allFinite(k.taps()));
        auto resp = fx.response();
        if (!prev.empty())
            for (size_t bin = 0; bin < resp.size(); ++bin)
                worst = std::max(worst, std::abs(resp[bin] - prev[bin]));
        prev = std::move(resp);
    }
    return worst;
}

} // namespace

// Acceptance 8b: scan sweep 0 -> 1 in 32 steps -> response varies continuously. The morph
// table's normalization reference (the dark frame's fundamental) vanishes to exactly zero
// at scan 1, so the response legitimately moves fast there; continuity is asserted the
// robust way: jumps must SHRINK when the step is refined (a genuine discontinuity would
// not), plus an absolute sanity bound at 32 steps.
TEST_CASE("scan sweep varies the response continuously", "[kernel][morph]") {
    auto morph = ftt::makeTwoFrameMorphTable();
    auto table = ftc::WavetableData::analyze(morph, 2, "morph");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    const float j32 = maxSweepJump(fx, table.get(), PhaseMode::Minimum, 32);
    const float j64 = maxSweepJump(fx, table.get(), PhaseMode::Minimum, 64);
    const float j128 = maxSweepJump(fx, table.get(), PhaseMode::Minimum, 128);
    INFO("max linear jump: 32 steps " << j32 << ", 64 steps " << j64 << ", 128 steps "
                                      << j128);
    CHECK(j32 < 0.9f);                   // sane absolute bound at the acceptance step count
    CHECK(j64 <= 0.65f * j32 + 0.01f);   // halving the step ~halves the jump...
    CHECK(j128 <= 0.65f * j64 + 0.01f);  // ...so there is no scan discontinuity

    // Linear mode reconstructs the windowed target exactly (no truncation ripple), so the
    // audible-band dB step can be bounded directly.
    std::vector<float> prev;
    float worstDb = 0.0f;
    for (int i = 0; i <= 32; ++i) {
        fx.make(table.get(), PhaseMode::Linear, static_cast<float>(i) / 32.0f, 4000.0f, 0.0f);
        auto resp = fx.response();
        if (!prev.empty())
            for (size_t bin = 0; bin < resp.size(); ++bin) {
                if (prev[bin] < 1e-2f || resp[bin] < 1e-2f)
                    continue; // only where both steps are above -40 dB re the 0 dB peak
                worstDb = std::max(worstDb, std::abs(ftk::dB(resp[bin]) - ftk::dB(prev[bin])));
            }
        prev = std::move(resp);
    }
    // ~4 dB of this is the one step where the normalization reference switches from the
    // vanishing dark-frame fundamental to the bright-frame peak (a kink of max(), continuous
    // but fast at 1/32 resolution) — everything rescales by the peak ratio at once.
    INFO("worst adjacent-step dB jump (Linear, above -40 dB) = " << worstDb);
    CHECK(worstDb < 5.0f);
}

// Acceptance 9: extreme cutoffs, silent frames and 1-frame tables never produce NaN/inf.
TEST_CASE("extreme cutoffs and degenerate tables stay finite", "[kernel][robust]") {
    auto saw = ftt::makeSawFrame(512);
    auto sawTable = ftc::WavetableData::analyze(saw, 1, "saw");
    std::vector<float> silent(static_cast<size_t>(ftt::kFrameLength), 0.0f);
    auto silentTable = ftc::WavetableData::analyze(silent, 1, "silent");
    auto morph = ftt::makeTwoFrameMorphTable();
    auto morphTable = ftc::WavetableData::analyze(morph, 2, "morph");
    REQUIRE(sawTable != nullptr);
    REQUIRE(silentTable != nullptr);
    REQUIRE(morphTable != nullptr);

    const PhaseMode modes[] = {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original,
                               PhaseMode::Raw};

    for (double fs : {44100.0, 96000.0}) {
        ftk::GenFixture fx(fs);
        for (const auto* table : {sawTable.get(), silentTable.get(), morphTable.get()}) {
            for (PhaseMode mode : modes) {
                for (float fc : {20.0f, 20000.0f}) {
                    for (float r : {-1.0f, 1.0f}) {
                        auto& k = fx.make(table, mode, 0.5f, fc, r);
                        INFO("fs=" << fs << " table=" << table->name()
                                   << " mode=" << static_cast<int>(mode) << " fc=" << fc
                                   << " r=" << r);
                        REQUIRE(k.length() == fx.L);
                        REQUIRE(ftk::allFinite(k.taps()));
                    }
                }
            }
        }
    }

    // Silent frame -> floored response: taps stay tiny, never boosted to unity.
    ftk::GenFixture fx(48000.0);
    for (PhaseMode mode : modes) {
        auto& k = fx.make(silentTable.get(), mode, 0.0f, 1000.0f, 0.0f);
        float mx = 0.0f;
        for (float x : k.taps())
            mx = std::max(mx, std::abs(x));
        INFO("mode=" << static_cast<int>(mode) << " max |tap| = " << mx);
        CHECK(mx <= 1e-4f);
    }

    // 1-frame table: scan is a no-op — any scan yields the scan-0 kernel exactly.
    auto& kA = fx.make(sawTable.get(), PhaseMode::Minimum, 0.0f, 1000.0f, 0.0f);
    std::vector<float> ref(kA.taps().begin(), kA.taps().end());
    auto& kB = fx.make(sawTable.get(), PhaseMode::Minimum, 0.7f, 1000.0f, 0.0f);
    REQUIRE(std::memcmp(ref.data(), kB.data(), ref.size() * sizeof(float)) == 0);
}

// Frozen-header contract: nullptr table -> pass-through kernel (unit impulse for latency-0
// modes, centered impulse for L/2 modes).
TEST_CASE("no wavetable produces a pass-through kernel", "[kernel][robust]") {
    ftk::GenFixture fx(48000.0);
    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Raw}) {
        auto& k = fx.make(nullptr, mode, 0.0f, 1000.0f, 0.0f);
        REQUIRE(k.length() == fx.L);
        CHECK(k.latencySamples() == 0);
        CHECK(k.taps()[0] == 1.0f);
        CHECK(ftk::totalEnergy(k.taps()) == 1.0);
    }
    for (PhaseMode mode : {PhaseMode::Linear, PhaseMode::Original}) {
        auto& k = fx.make(nullptr, mode, 0.0f, 1000.0f, 0.0f);
        REQUIRE(k.length() == fx.L);
        CHECK(k.latencySamples() == fx.L / 2);
        CHECK(k.taps()[static_cast<size_t>(fx.L / 2)] == 1.0f);
        CHECK(ftk::totalEnergy(k.taps()) == 1.0);
    }
}

TEST_CASE("latencyForMode reports the frozen contract", "[kernel]") {
    for (int L : {2048, 4096, 8192}) {
        CHECK(ftc::KernelGenerator::latencyForMode(PhaseMode::Minimum, L) == 0);
        CHECK(ftc::KernelGenerator::latencyForMode(PhaseMode::Raw, L) == 0);
        CHECK(ftc::KernelGenerator::latencyForMode(PhaseMode::Linear, L) == L / 2);
        CHECK(ftc::KernelGenerator::latencyForMode(PhaseMode::Original, L) == L / 2);
    }
}

// Determinism: identical requests produce bit-identical kernels (offline renders and
// crossfade retries rely on this).
TEST_CASE("generation is deterministic", "[kernel]") {
    auto morph = ftt::makeTwoFrameMorphTable();
    auto table = ftc::WavetableData::analyze(morph, 2, "morph");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original,
                           PhaseMode::Raw}) {
        auto& k1 = fx.make(table.get(), mode, 0.4f, 3000.0f, 0.6f);
        std::vector<float> ref(k1.taps().begin(), k1.taps().end());
        fx.make(table.get(), mode, 0.9f, 12000.0f, -0.5f); // disturb internal scratch
        auto& k2 = fx.make(table.get(), mode, 0.4f, 3000.0f, 0.6f);
        INFO("mode=" << static_cast<int>(mode));
        REQUIRE(std::memcmp(ref.data(), k2.data(), ref.size() * sizeof(float)) == 0);
    }
}
