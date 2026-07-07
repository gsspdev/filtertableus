// FactoryGenerators acceptance tests (brief B, acceptance 6).
#include <catch2/catch_test_macros.hpp>

#include <cstring>

#include "WtTestHelpers.h"
#include "ftus/FactoryTables.h"
#include "wavetable/FrameOps.h"

using namespace wt_test;

namespace {
constexpr int kFrame = ftus::wtio::kFrameLength;
}

TEST_CASE("wt: all 12 factory tables meet the frame contract", "[wavetable][factory]") {
    for (int i = 0; i < ftus::kNumFactoryTables; ++i) {
        const auto id = static_cast<ftus::FactoryTableId>(i);
        INFO("table: " << ftus::factoryTableIdString(id));

        const ftus::RawTable table = ftus::generateFactoryTable(id);

        REQUIRE(table.numFrames >= 16);
        REQUIRE(table.numFrames <= 256);
        REQUIRE(table.samples.size() ==
                static_cast<size_t>(table.numFrames) * static_cast<size_t>(kFrame));
        REQUIRE(!table.name.empty());
        REQUIRE(allFinite(table.samples));

        for (int f = 0; f < table.numFrames; ++f) {
            const float* frame = table.samples.data() + static_cast<size_t>(f) * kFrame;
            const std::span<const float> span{frame, static_cast<size_t>(kFrame)};
            INFO("frame " << f);
            REQUIRE(peakAbs(span) <= 1.0f);
            REQUIRE(peakAbs(span) > 0.1f); // frames actually carry signal
            REQUIRE(std::abs(frame[kFrame - 1] - frame[0]) < 0.05f); // seam continuity
        }
    }
}

TEST_CASE("wt: factory tables are deterministic across calls", "[wavetable][factory]") {
    for (int i = 0; i < ftus::kNumFactoryTables; ++i) {
        const auto id = static_cast<ftus::FactoryTableId>(i);
        INFO("table: " << ftus::factoryTableIdString(id));

        const auto a = ftus::generateFactoryTable(id);
        const auto b = ftus::generateFactoryTable(id);
        REQUIRE(a.numFrames == b.numFrames);
        REQUIRE(a.name == b.name);
        REQUIRE(a.samples.size() == b.samples.size());
        REQUIRE(std::memcmp(a.samples.data(), b.samples.data(),
                            a.samples.size() * sizeof(float)) == 0);
    }
}

TEST_CASE("wt: factory tables actually morph across frames", "[wavetable][factory]") {
    // First and last frames must differ audibly — a table that doesn't move is useless.
    for (int i = 0; i < ftus::kNumFactoryTables; ++i) {
        const auto id = static_cast<ftus::FactoryTableId>(i);
        INFO("table: " << ftus::factoryTableIdString(id));

        const auto table = ftus::generateFactoryTable(id);
        const float* first = table.samples.data();
        const float* last =
            table.samples.data() + static_cast<size_t>(table.numFrames - 1) * kFrame;
        double diff = 0.0;
        for (int n = 0; n < kFrame; ++n)
            diff += std::abs(static_cast<double>(first[n]) - static_cast<double>(last[n]));
        diff /= kFrame;
        REQUIRE(diff > 0.01); // mean absolute difference across the morph
    }
}

TEST_CASE("wt: factory frame counts match their designs", "[wavetable][factory]") {
    REQUIRE(ftus::generateFactoryTable(ftus::FactoryTableId::AnalogMorph).numFrames == 64);
    REQUIRE(ftus::generateFactoryTable(ftus::FactoryTableId::Pwm).numFrames == 128);
    REQUIRE(ftus::generateFactoryTable(ftus::FactoryTableId::VowelMorph).numFrames == 128);
}

namespace {

/// Spectral centroid (in harmonic number) over the first `maxBin` harmonics.
double centroid(const float* frame, int maxBin) {
    double num = 0.0, den = 0.0;
    for (int k = 1; k <= maxBin; ++k) {
        const double m = dftMagnitude({frame, static_cast<size_t>(kFrame)}, k);
        num += k * m;
        den += m;
    }
    return den > 0.0 ? num / den : 0.0;
}

const float* frameOf(const ftus::RawTable& t, int f) {
    return t.samples.data() + static_cast<size_t>(f) * kFrame;
}

} // namespace

TEST_CASE("wt: factory tables morph in their designed direction", "[wavetable][factory]") {
    using ftus::FactoryTableId;

    SECTION("SubBloom opens upward") {
        const auto t = ftus::generateFactoryTable(FactoryTableId::SubBloom);
        const double first = centroid(frameOf(t, 0), 64);
        const double last = centroid(frameOf(t, t.numFrames - 1), 64);
        REQUIRE(first < 2.0);          // starts as (nearly) a pure fundamental
        REQUIRE(last > 3.0 * first);   // ends bright
    }

    SECTION("HarmonicLadder climbs from 1 partial to many") {
        const auto t = ftus::generateFactoryTable(FactoryTableId::HarmonicLadder);
        const std::span<const float> firstFrame{frameOf(t, 0), static_cast<size_t>(kFrame)};
        const std::span<const float> lastFrame{frameOf(t, t.numFrames - 1),
                                               static_cast<size_t>(kFrame)};
        // Frame 0 is a sine: everything above bin 1 is negligible.
        REQUIRE(dftMagnitude(firstFrame, 2) < 0.01 * dftMagnitude(firstFrame, 1));
        // Final frame carries real energy high up the ladder.
        REQUIRE(dftMagnitude(lastFrame, 32) > 0.05 * dftMagnitude(lastFrame, 1));
    }

    SECTION("Pwm grows even harmonics as the pulse narrows") {
        const auto t = ftus::generateFactoryTable(FactoryTableId::Pwm);
        const std::span<const float> firstFrame{frameOf(t, 0), static_cast<size_t>(kFrame)};
        const std::span<const float> lastFrame{frameOf(t, t.numFrames - 1),
                                               static_cast<size_t>(kFrame)};
        // 50% duty: even harmonics absent.
        REQUIRE(dftMagnitude(firstFrame, 2) < 0.02 * dftMagnitude(firstFrame, 1));
        // 3% duty: 2nd harmonic rivals the fundamental.
        REQUIRE(dftMagnitude(lastFrame, 2) > 0.5 * dftMagnitude(lastFrame, 1));
    }

    SECTION("MetalCluster lives high") {
        const auto t = ftus::generateFactoryTable(FactoryTableId::MetalCluster);
        REQUIRE(centroid(frameOf(t, 0), 512) > 15.0);
    }

    SECTION("AnalogMorph ends square-ish (odd harmonics, ~1/k)") {
        const auto t = ftus::generateFactoryTable(FactoryTableId::AnalogMorph);
        const std::span<const float> lastFrame{frameOf(t, t.numFrames - 1),
                                               static_cast<size_t>(kFrame)};
        const double h1 = dftMagnitude(lastFrame, 1);
        REQUIRE(dftMagnitude(lastFrame, 2) < 0.02 * h1);  // evens gone
        REQUIRE(dftMagnitude(lastFrame, 3) > 0.25 * h1);  // 1/3 within tolerance
        REQUIRE(dftMagnitude(lastFrame, 3) < 0.40 * h1);
    }
}
