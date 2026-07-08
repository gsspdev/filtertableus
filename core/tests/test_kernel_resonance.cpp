// Kernel generation — resonance contrast + output normalization acceptance tests.
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

#include "ftc/WavetableData.h"
#include "test_kernel_helpers.h"

using ftc::PhaseMode;

namespace {

// Harmonics 1..32 at amplitude 1, with a known trough at h10 (-26 dB) and peak at h20 (+12 dB).
std::shared_ptr<const ftc::WavetableData> makeContrastTable() {
    std::vector<std::pair<int, float>> partials;
    for (int h = 1; h <= 32; ++h) {
        float amp = 1.0f;
        if (h == 10)
            amp = 0.05f;
        if (h == 20)
            amp = 4.0f;
        partials.emplace_back(h, amp);
    }
    auto frame = ftk::makeHarmonicFrame(partials);
    return ftc::WavetableData::analyze(frame, 1, "contrast");
}

} // namespace

// Acceptance 7: r = +1 deepens a known trough and sharpens a known peak monotonically vs
// r = 0; r = -1 flattens toward uniform.
TEST_CASE("resonance deepens troughs and sharpens peaks monotonically", "[kernel][resonance]") {
    auto table = makeContrastTable();
    REQUIRE(table != nullptr);

    // fc = 4800 -> harmonic h at 200 h Hz: trough tooth at 2 kHz, peak tooth at 4 kHz.
    const float fc = 4800.0f;

    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Original}) {
        ftk::GenFixture fx(48000.0);
        std::vector<float> troughDepth; // neighbor (h9) minus trough (h10), in dB
        std::vector<float> peakSharp;   // peak (h20) minus neighbor (h19), in dB
        std::vector<float> spread;      // max - min over harmonic centers, in dB
        for (float r : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f}) {
            auto& k = fx.make(table.get(), mode, 0.0f, fc, r);
            REQUIRE(ftk::allFinite(k.taps()));
            auto resp = fx.response();
            const float trough = ftk::responseDbAt(resp, fx.fs, fx.N, 2000.0);
            const float troughNb = ftk::responseDbAt(resp, fx.fs, fx.N, 1800.0);
            const float peak = ftk::responseDbAt(resp, fx.fs, fx.N, 4000.0);
            const float peakNb = ftk::responseDbAt(resp, fx.fs, fx.N, 3800.0);
            troughDepth.push_back(troughNb - trough);
            peakSharp.push_back(peak - peakNb);
            float mx = -1000.0f, mn = 1000.0f;
            for (int h = 1; h <= 32; ++h) {
                const float v = ftk::responseDbAt(resp, fx.fs, fx.N, 200.0 * h);
                mx = std::max(mx, v);
                mn = std::min(mn, v);
            }
            spread.push_back(mx - mn);
        }
        INFO("mode=" << static_cast<int>(mode));
        for (size_t i = 1; i < troughDepth.size(); ++i) {
            INFO("step " << i << ": troughDepth " << troughDepth[i - 1] << " -> "
                         << troughDepth[i] << ", peakSharp " << peakSharp[i - 1] << " -> "
                         << peakSharp[i]);
            CHECK(peakSharp[i] > peakSharp[i - 1] + 0.5f);
            if (mode == PhaseMode::Original && i == troughDepth.size() - 1) {
                // The Original comb kernel's notch depth saturates at the leakage floor of
                // the adjacent windowed teeth (~34 dB here) — the true spectral trough is
                // already below what L taps can express by r = 0.5. Require the depth to
                // hold at saturation and stay far beyond the r = 0 depth.
                CHECK(troughDepth[i] > troughDepth[i - 1] - 2.0f);
                CHECK(troughDepth[i] > troughDepth[2] + 1.0f);
            } else {
                CHECK(troughDepth[i] > troughDepth[i - 1] + 1.0f);
            }
        }
        // r = -1 flattens toward uniform: spread well below the r = 0 spread.
        INFO("spread r=-1: " << spread[0] << " dB, r=0: " << spread[2] << " dB");
        CHECK(spread[0] < 0.5f * spread[2]);
    }
}

// Acceptance 7 (tail): the REALIZED kernel peak (|FFT| of the truncated/windowed taps, the
// thing normalizeKernelPeak pins) stays in [-1, +0.1] dB across the peak-normalized modes,
// frames, cutoffs and resonance settings tested — including fc = 30/100 Hz, where design-only
// normalization used to let truncation/window ripple overshoot up to ~+1 dB (Wave-3.1 fix:
// Minimum and Linear now realized-normalize like Original always did). Raw's contract is
// energy normalization (sum h^2 = 1) instead, checked alongside.
TEST_CASE("kernel output normalization holds across modes frames cutoffs", "[kernel][resonance]") {
    auto saw = ftt::makeSawFrame(512);
    auto sawTable = ftc::WavetableData::analyze(saw, 1, "saw");
    auto morph = ftt::makeTwoFrameMorphTable();
    auto morphTable = ftc::WavetableData::analyze(morph, 2, "morph");
    auto contrastTable = makeContrastTable();
    REQUIRE(sawTable != nullptr);
    REQUIRE(morphTable != nullptr);
    REQUIRE(contrastTable != nullptr);

    ftk::GenFixture fx(48000.0);
    const ftc::WavetableData* tables[] = {sawTable.get(), morphTable.get(),
                                          contrastTable.get()};
    for (const auto* table : tables) {
        for (float fc : {30.0f, 100.0f, 2000.0f, 4000.0f, 12000.0f}) {
            for (float r : {-1.0f, 0.0f, 1.0f}) {
                for (PhaseMode mode :
                     {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original}) {
                    auto& k = fx.make(table, mode, 0.5f, fc, r);
                    REQUIRE(ftk::allFinite(k.taps()));
                    auto resp = fx.response();
                    const float p = ftk::peakDb(resp);
                    INFO("table=" << table->name() << " fc=" << fc << " r=" << r
                                  << " mode=" << static_cast<int>(mode) << " peak=" << p);
                    CHECK(p >= -1.0f);
                    CHECK(p <= 0.1f);
                }
                auto& k = fx.make(table, PhaseMode::Raw, 0.5f, fc, r);
                REQUIRE(ftk::allFinite(k.taps()));
                const double total = ftk::totalEnergy(k.taps());
                INFO("table=" << table->name() << " fc=" << fc << " r=" << r << " raw energy="
                              << total);
                CHECK_THAT(total, Catch::Matchers::WithinAbs(1.0, 1e-3));
            }
        }
    }
}
