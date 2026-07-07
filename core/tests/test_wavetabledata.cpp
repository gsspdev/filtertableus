#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "ftc/WavetableData.h"
#include "helpers/TestSignals.h"

TEST_CASE("analyze rejects invalid input", "[wavetable]") {
    std::vector<float> tooShort(100, 0.0f);
    REQUIRE(ftc::WavetableData::analyze(tooShort, 1, "bad") == nullptr);
    std::vector<float> frame(ftt::kFrameLength, 0.0f);
    REQUIRE(ftc::WavetableData::analyze(frame, 0, "bad") == nullptr);
    REQUIRE(ftc::WavetableData::analyze(frame, 2, "bad") == nullptr);
}

TEST_CASE("analyze of a saw frame yields 1/k harmonic magnitudes", "[wavetable]") {
    auto saw = ftt::makeSawFrame(64);
    auto table = ftc::WavetableData::analyze(saw, 1, "saw");
    REQUIRE(table != nullptr);
    REQUIRE(table->numFrames() == 1);
    auto mags = table->magnitudes(0);
    REQUIRE(mags.size() == static_cast<size_t>(ftc::WavetableData::kNumBins));
    // sin amplitude 1/k at harmonic k -> |X[k]| = (1/k) * N/2; check ratios.
    REQUIRE(mags[1] > 0.0f);
    REQUIRE_THAT(mags[1] / mags[2], Catch::Matchers::WithinRel(2.0f, 0.01f));
    REQUIRE_THAT(mags[1] / mags[4], Catch::Matchers::WithinRel(4.0f, 0.01f));
    // absolute scale: |X[1]| ~= N/2
    REQUIRE_THAT(mags[1], Catch::Matchers::WithinRel(1024.0f, 0.01f));
    // above harmonic 64 (and DC) ~ zero
    REQUIRE(mags[0] < 1.0f);
    REQUIRE(mags[100] < 1.0f);
    REQUIRE(table->maxMagnitude() >= mags[1]);
}

TEST_CASE("frame accessors clamp indices", "[wavetable]") {
    auto tableData = ftt::makeTwoFrameMorphTable();
    auto table = ftc::WavetableData::analyze(tableData, 2, "morph");
    REQUIRE(table != nullptr);
    REQUIRE(table->numFrames() == 2);
    REQUIRE(table->frame(-5).data() == table->frame(0).data());
    REQUIRE(table->frame(99).data() == table->frame(1).data());
    REQUIRE(table->spectrum(0).size() == static_cast<size_t>(ftc::WavetableData::kNumBins));
    REQUIRE(table->name() == "morph");
}
