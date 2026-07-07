// Kernel generation — spectral path (Minimum / Linear) acceptance tests.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/WavetableData.h"
#include "test_kernel_helpers.h"

using ftc::PhaseMode;

// Acceptance 1: single-harmonic-24 frame, fc = 1 kHz, res 0 -> measured magnitude peak at
// 1.0 kHz +/- 1 design bin, for Minimum AND Linear, at fs in {44100, 48000, 96000}.
TEST_CASE("spectral kernel peak lands at cutoff across sample rates", "[kernel][spectral]") {
    auto frame = ftt::makeSineFrame(24);
    auto table = ftc::WavetableData::analyze(frame, 1, "h24");
    REQUIRE(table != nullptr);

    for (double fs : {44100.0, 48000.0, 96000.0}) {
        for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear}) {
            ftk::GenFixture fx(fs);
            auto& k = fx.make(table.get(), mode, 0.0f, 1000.0f, 0.0f);
            INFO("fs=" << fs << " mode=" << static_cast<int>(mode));
            REQUIRE(k.length() == fx.L);
            REQUIRE(ftk::allFinite(k.taps()));
            auto resp = fx.response();
            const int am = ftk::argmax(resp);
            const double expected = fx.exactBinOf(1000.0);
            CHECK(std::abs(am - expected) <= 1.55); // within +/- 1 design bin of 1 kHz
        }
    }
}

// Acceptance 2a: harmonic-1 frame -> peak at fc/24.
TEST_CASE("harmonic-1 frame peaks at cutoff/24", "[kernel][spectral]") {
    auto frame = ftt::makeSineFrame(1);
    auto table = ftc::WavetableData::analyze(frame, 1, "h1");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear}) {
        fx.make(table.get(), mode, 0.0f, 4800.0f, 0.0f);
        auto resp = fx.response();
        const int am = ftk::argmax(resp);
        const double expected = fx.exactBinOf(4800.0 / 24.0); // 200 Hz
        INFO("mode=" << static_cast<int>(mode) << " argmax=" << am << " expected=" << expected);
        CHECK(std::abs(am - expected) <= 1.55);
    }
}

// Acceptance 2b: saw frame -> response trend ~ -6 dB/oct sampled at harmonic centers.
TEST_CASE("saw frame response falls -6 dB per octave", "[kernel][spectral]") {
    auto saw = ftt::makeSawFrame(512);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);

    ftk::GenFixture fx(48000.0);
    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear}) {
        fx.make(table.get(), mode, 0.0f, 4800.0f, 0.0f);
        auto resp = fx.response();
        // Harmonic h sits at h * fc / 24 = 200 h Hz. One octave apart -> -6.02 dB.
        for (double f : {400.0, 800.0, 1600.0, 3200.0}) {
            const float lo = ftk::responseDbAt(resp, fx.fs, fx.N, f);
            const float hi = ftk::responseDbAt(resp, fx.fs, fx.N, 2.0 * f);
            INFO("mode=" << static_cast<int>(mode) << " f=" << f << " lo=" << lo
                         << " hi=" << hi);
            CHECK(std::abs((hi - lo) + 6.02f) <= 1.5f);
        }
    }
}

// Acceptance 3: Minimum in-band |FFT(kernel)| within +/-1 dB of the design target where the
// target is above -60 dB; cumulative energy >= 70% in the first L/8 taps.
TEST_CASE("minimum phase matches design target and is front-loaded", "[kernel][spectral]") {
    auto saw = ftt::makeSawFrame(512);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);

    const double fs = 48000.0;
    const double fc = 4800.0;
    ftk::GenFixture fx(fs);
    auto& k = fx.make(table.get(), PhaseMode::Minimum, 0.0f, static_cast<float>(fc), 0.0f);
    auto resp = fx.response();

    // Independent target: harmonic-24 mapping of the analyzed magnitudes, peak-normalized.
    // (Default policies: linear-magnitude lerp is a no-op for one frame; res = 0.)
    auto mags = table->magnitudes(0);
    const int bins = fx.N / 2 + 1;
    std::vector<double> target(static_cast<size_t>(bins), 0.0);
    const double hStep = 24.0 * fs / (fx.N * fc);
    for (int b = 0; b < bins; ++b) {
        const double h = b * hStep;
        if (h > 1024.0)
            break;
        double v;
        if (h >= 1.0) {
            const int j = static_cast<int>(h);
            const int j1 = j < 1024 ? j + 1 : 1024;
            v = mags[static_cast<size_t>(j)]
                + (h - j) * (mags[static_cast<size_t>(j1)] - mags[static_cast<size_t>(j)]);
        } else {
            v = mags[0] + h * (mags[1] - mags[0]); // InterpToDC
        }
        if (h > 1016.0)
            v *= 0.5 * (1.0 + std::cos(3.14159265358979323846 * (h - 1016.0) / 8.0));
        target[static_cast<size_t>(b)] = v;
    }
    double peak = 0.0;
    for (double v : target)
        peak = std::max(peak, v);
    REQUIRE(peak > 0.0);
    for (double& v : target)
        v /= peak;

    int compared = 0;
    float worst = 0.0f;
    for (int b = 0; b < bins; ++b) {
        const double h = b * hStep;
        if (h < 1.0 || h > 1016.0)
            continue; // in-band = harmonic band up to the edge taper
        if (target[static_cast<size_t>(b)] < 1e-3)
            continue; // only where target > -60 dB
        const float err = std::abs(
            ftk::dB(resp[static_cast<size_t>(b)])
            - 20.0f * std::log10(static_cast<float>(target[static_cast<size_t>(b)])));
        worst = std::max(worst, err);
        ++compared;
    }
    INFO("compared " << compared << " bins, worst |err| = " << worst << " dB");
    REQUIRE(compared > 100);
    CHECK(worst <= 1.0f);

    // Regression bound: minimum phase concentrates energy at the front.
    const double total = ftk::totalEnergy(k.taps());
    const double head = ftk::energyOfFirst(k.taps(), fx.L / 8);
    REQUIRE(total > 0.0);
    CHECK(head / total >= 0.70);

    CHECK(k.latencySamples() == 0);
}

// Acceptance 4: Linear taps[i] == taps[L-1-i] within 1e-6; latency L/2.
TEST_CASE("linear phase kernel is tap-symmetric with L/2 latency", "[kernel][spectral]") {
    auto saw = ftt::makeSawFrame(512);
    auto sawTable = ftc::WavetableData::analyze(saw, 1, "saw");
    auto morph = ftt::makeTwoFrameMorphTable();
    auto morphTable = ftc::WavetableData::analyze(morph, 2, "morph");
    REQUIRE(sawTable != nullptr);
    REQUIRE(morphTable != nullptr);

    ftk::GenFixture fx(48000.0);
    struct Case {
        const ftc::WavetableData* table;
        float scan, fc, res;
    };
    const Case cases[] = {{sawTable.get(), 0.0f, 1000.0f, 0.0f},
                          {morphTable.get(), 0.3f, 4000.0f, 0.5f},
                          {morphTable.get(), 0.9f, 12000.0f, -0.7f}};
    for (const auto& c : cases) {
        auto& k = fx.make(c.table, PhaseMode::Linear, c.scan, c.fc, c.res);
        REQUIRE(k.length() == fx.L);
        REQUIRE(ftk::allFinite(k.taps()));
        CHECK(k.latencySamples() == fx.L / 2);
        auto taps = k.taps();
        float worst = 0.0f;
        for (int i = 0; i < fx.L / 2; ++i)
            worst = std::max(worst,
                             std::abs(taps[static_cast<size_t>(i)]
                                      - taps[static_cast<size_t>(fx.L - 1 - i)]));
        INFO("scan=" << c.scan << " fc=" << c.fc << " res=" << c.res << " worst=" << worst);
        CHECK(worst <= 1e-6f);
    }
}
