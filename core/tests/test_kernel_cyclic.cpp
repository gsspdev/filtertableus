// Kernel generation — cyclic path (Original / Raw) acceptance tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

#include "ftc/WavetableData.h"
#include "test_kernel_helpers.h"

using ftc::PhaseMode;

// Acceptance 5a: sine-at-harmonic-1 frame -> comb fundamental at fc/24.
TEST_CASE("original mode places the comb fundamental at cutoff/24", "[kernel][cyclic]") {
    auto frame = ftt::makeSineFrame(1);
    auto table = ftc::WavetableData::analyze(frame, 1, "h1");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    auto& k = fx.make(table.get(), PhaseMode::Original, 0.0f, 4800.0f, 0.0f);
    REQUIRE(ftk::allFinite(k.taps()));
    auto resp = fx.response();
    const int am = ftk::argmax(resp);
    const double expected = fx.exactBinOf(200.0); // 4800 / 24
    INFO("argmax=" << am << " expected=" << expected);
    CHECK(std::abs(am - expected) <= 2.05);
    CHECK(k.latencySamples() == fx.L / 2);
}

// Multi-harmonic frame -> comb teeth at multiples of fc/24, gaps between them.
TEST_CASE("original mode comb has teeth at harmonic multiples", "[kernel][cyclic]") {
    auto saw = ftt::makeSawFrame(64);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    fx.make(table.get(), PhaseMode::Original, 0.0f, 4800.0f, 0.0f);
    auto resp = fx.response();
    for (int h = 1; h <= 3; ++h) {
        const float tooth = ftk::maxResponseDbNear(resp, fx.fs, fx.N, 200.0 * h, 2);
        const float gap = ftk::maxResponseDbNear(resp, fx.fs, fx.N, 200.0 * h + 100.0, 2);
        INFO("h=" << h << " tooth=" << tooth << " gap=" << gap);
        CHECK(tooth > gap + 10.0f);
    }
}

// Acceptance 5b: no content above Nyquist at high fc — harmonics mapping above fs/2 are
// removed (not aliased back), unlike Raw which keeps them (its contract).
TEST_CASE("original mode band-limits harmonics above Nyquist", "[kernel][cyclic]") {
    // Harmonic 1 -> 625 Hz (kept); harmonic 48 -> 30 kHz (above 22.05 kHz Nyquist).
    auto frame = ftk::makeHarmonicFrame({{1, 1.0f}, {48, 1.0f}});
    auto table = ftc::WavetableData::analyze(frame, 1, "h1h48");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(44100.0);
    const float fc = 15000.0f;
    const double aliasHz = 44100.0 - 30000.0; // 14.1 kHz fold-back of the h48 partial

    fx.make(table.get(), PhaseMode::Original, 0.0f, fc, 0.0f);
    auto respO = fx.response();
    const float peakO = ftk::peakDb(respO);
    const float fundO = ftk::maxResponseDbNear(respO, fx.fs, fx.N, 625.0, 2);
    const float aliasO = ftk::maxResponseDbNear(respO, fx.fs, fx.N, aliasHz, 3);
    INFO("original: peak=" << peakO << " fund=" << fundO << " alias=" << aliasO);
    CHECK(fundO > peakO - 1.0f);          // the surviving harmonic carries the peak
    CHECK(aliasO < peakO - 60.0f);        // nothing folded back

    fx.make(table.get(), PhaseMode::Raw, 0.0f, fc, 0.0f);
    auto respR = fx.response();
    const float peakR = ftk::peakDb(respR);
    const float aliasR = ftk::maxResponseDbNear(respR, fx.fs, fx.N, aliasHz, 3);
    INFO("raw: peak=" << peakR << " alias=" << aliasR);
    CHECK(aliasR > peakR - 20.0f);        // Raw keeps the aliased partial (mode contract)
}

// Acceptance 5c: Original latency is L/2 and CONSTANT across cutoff.
TEST_CASE("original mode latency is L/2 at every cutoff", "[kernel][cyclic]") {
    auto saw = ftt::makeSawFrame(64);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    for (float fc : {100.0f, 1000.0f, 10000.0f}) {
        auto& k = fx.make(table.get(), PhaseMode::Original, 0.0f, fc, 0.0f);
        INFO("fc=" << fc);
        REQUIRE(ftk::allFinite(k.taps()));
        CHECK(k.latencySamples() == fx.L / 2); // metadata never varies with cutoff
        const double centroid = ftk::energyCentroid(k.taps());
        INFO("energy centroid=" << centroid);
        CHECK(std::abs(centroid - fx.L / 2.0) <= fx.L / 8.0);
    }
}

// Acceptance 6: Raw is causal (latency 0, front-loaded energy) and energy-normalized.
TEST_CASE("raw mode is causal and energy normalized", "[kernel][cyclic]") {
    auto saw = ftt::makeSawFrame(64);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    for (float fc : {250.0f, 1000.0f, 8000.0f}) {
        auto& k = fx.make(table.get(), PhaseMode::Raw, 0.0f, fc, 0.0f);
        INFO("fc=" << fc);
        REQUIRE(ftk::allFinite(k.taps()));
        CHECK(k.latencySamples() == 0);
        const double total = ftk::totalEnergy(k.taps());
        CHECK_THAT(total, Catch::Matchers::WithinAbs(1.0, 1e-3));
        // Periodic fill from tap 0: the first L/8 taps hold a solid share of the energy.
        CHECK(ftk::energyOfFirst(k.taps(), fx.L / 8) / total > 0.05);
    }
}
