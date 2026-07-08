// Engine acceptance: per-mode latency exactness, reported latency, UI taps, lifecycle.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/EngineConfig.h"
#include "ftc/FilterTableEngine.h"
#include "test_engine_utils.h"

using ftc::FilterTableEngine;
using ftc::PhaseMode;
namespace fe = ftet;

namespace {

constexpr PhaseMode kAllModes[] = {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original,
                                   PhaseMode::Raw};

std::vector<float> impulseSignal(int n) {
    std::vector<float> v(static_cast<size_t>(n), 0.0f);
    v[0] = 1.0f;
    return v;
}

} // namespace

TEST_CASE("latencySamplesFor matches the kernel-generator design values", "[engine]") {
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Minimum, 48000.0) == 0);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Raw, 48000.0) == 0);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Linear, 48000.0) == 1024);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Original, 48000.0) == 1024);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Linear, 96000.0) == 2048);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Original, 192000.0) == 4096);
    CHECK(FilterTableEngine::latencySamplesFor(PhaseMode::Minimum, 192000.0) == 0);
}

// With no wavetable the generator emits an exact single-tap pass-through kernel (unit impulse
// at the mode's latency), so the impulse peak position is EXACT for every mode.
TEST_CASE("per-mode latency: impulse peak lands exactly at latencySamplesFor (no table)",
          "[engine]") {
    for (PhaseMode mode : kAllModes) {
        fe::Harness h;
        h.params.mode = mode;
        h.params.cutoffHz = 20000.0f;
        h.params.mix = 1.0f;
        h.prepare();

        const int expected = FilterTableEngine::latencySamplesFor(mode, fe::kFs);
        REQUIRE(h.engine.latencySamples() == expected);

        auto out = h.renderMono(impulseSignal(4096));
        REQUIRE(fe::allFinite(out));
        CHECK(fe::argMaxAbs(out) == expected);
        CHECK(out[static_cast<size_t>(expected)] > 0.9f);
        // Everything away from the single tap is FFT noise only.
        float off = 0.0f;
        for (size_t i = 0; i < out.size(); ++i)
            if (static_cast<int>(i) != expected)
                off = std::max(off, std::abs(out[i]));
        CHECK(off < 1e-4f);
        CHECK(h.engine.latencySamples() == expected);
    }
}

TEST_CASE("per-mode latency: impulse onset/peak with a real (flat) table", "[engine]") {
    SECTION("Minimum: onset at 0") {
        fe::Harness h;
        h.params.mode = PhaseMode::Minimum;
        h.params.cutoffHz = 20000.0f;
        h.params.mix = 1.0f;
        h.engine.setWavetable(fe::flatTable());
        h.prepare();
        auto out = h.renderMono(impulseSignal(4096));
        CHECK(fe::argMaxAbs(out) == 0);
        CHECK(h.engine.latencySamples() == 0);
    }
    SECTION("Linear: peak at L/2 (within the documented half-sample center)") {
        fe::Harness h;
        h.params.mode = PhaseMode::Linear;
        h.params.cutoffHz = 20000.0f;
        h.params.mix = 1.0f;
        h.engine.setWavetable(fe::flatTable());
        h.prepare();
        auto out = h.renderMono(impulseSignal(4096));
        const int peak = fe::argMaxAbs(out);
        // Default (FTUS_LINEAR_HALF_SAMPLE_CENTER=0 since Wave-3) centers at tap L/2; the
        // calibration variant (=1) is symmetric about (L-1)/2 (group delay L/2 - 0.5), where
        // the impulse peak may land on either neighbour of the reported latency.
        CHECK(peak >= fe::kL / 2 - 1);
        CHECK(peak <= fe::kL / 2);
        CHECK(h.engine.latencySamples() == fe::kL / 2);
    }
    SECTION("Original: peak exactly at L/2 (cutoff chosen so one cycle spans one kernel)") {
        fe::Harness h;
        h.params.mode = PhaseMode::Original;
        h.params.cutoffHz = 562.5f; // 24*fs/L -> resample rate 1.0, single cycle anchor
        h.params.mix = 1.0f;
        h.engine.setWavetable(fe::flatTable());
        h.prepare();
        auto out = h.renderMono(impulseSignal(4096));
        CHECK(fe::argMaxAbs(out) == fe::kL / 2);
        CHECK(h.engine.latencySamples() == fe::kL / 2);
    }
    SECTION("Raw: onset at 0") {
        fe::Harness h;
        h.params.mode = PhaseMode::Raw;
        h.params.cutoffHz = 562.5f;
        h.params.mix = 1.0f;
        h.engine.setWavetable(fe::flatTable());
        h.prepare();
        auto out = h.renderMono(impulseSignal(4096));
        CHECK(fe::argMaxAbs(out) == 0);
        CHECK(h.engine.latencySamples() == 0);
    }
}

TEST_CASE("unprepared engine outputs silence", "[engine]") {
    ftc::FilterTableEngine e;
    std::vector<float> l = fe::makeNoise(512, 7u);
    std::vector<float> r = l;
    float* chs[2] = {l.data(), r.data()};
    e.process(chs, 2, 512, {}, {});
    for (int i = 0; i < 512; ++i) {
        CHECK(l[static_cast<size_t>(i)] == 0.0f);
        CHECK(r[static_cast<size_t>(i)] == 0.0f);
    }
    CHECK(e.latencySamples() == 0);
}

TEST_CASE("UI taps: response curve, scan/env atomics, table mirror, GC", "[engine]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Minimum;
    h.params.cutoffHz = 20000.0f;
    h.params.scan = 0.7f;
    auto table = fe::flatTable();
    h.engine.setWavetable(table);
    h.prepare();

    // Initial curve published synchronously by prepare(): flat table + high cutoff -> ~0 dB.
    ftc::ResponseCurve rc;
    REQUIRE(h.engine.readResponseCurve(rc));
    for (int i = 0; i < ftc::ResponseCurve::kNumPoints; ++i) {
        CHECK(rc.db[static_cast<size_t>(i)] <= 0.5f);
        CHECK(rc.db[static_cast<size_t>(i)] >= -1.0f);
    }

    // A dark low-cutoff response must fall off hard at the top of the grid.
    fe::Harness dark;
    dark.params.mode = PhaseMode::Minimum;
    dark.params.cutoffHz = 200.0f; // top harmonic 1024 maps to ~8.5 kHz -> 20 kHz is closed
    dark.engine.setWavetable(fe::morphTable());
    dark.prepare();
    ftc::ResponseCurve rc2;
    REQUIRE(dark.engine.readResponseCurve(rc2));
    float peak = -1000.0f;
    for (int i = 0; i < ftc::ResponseCurve::kNumPoints; ++i)
        peak = std::max(peak, rc2.db[static_cast<size_t>(i)]);
    CHECK(peak >= -20.0f);
    CHECK(peak <= 0.5f);
    CHECK(rc2.db[ftc::ResponseCurve::kNumPoints - 1] < -60.0f);

    // Scan atomic follows the (smoothed, post-mod) scan; env rises on loud input.
    auto noise = fe::makeNoise(24576, 42u, 0.9f);
    h.renderMono(noise);
    CHECK(std::abs(h.engine.currentScan() - 0.7f) < 0.01f);
    CHECK(h.engine.envValue() > 0.05f);
    CHECK(h.engine.envValue() <= 1.0f);

    // Message-thread mirror + GC smoke.
    REQUIRE(h.engine.currentTableForUi() == table);
    auto t2 = fe::morphTable();
    h.engine.setWavetable(t2);
    CHECK(h.engine.currentTableForUi() == t2);
    h.renderMono(fe::makeNoise(2048, 3u));
    h.engine.collectGarbage(); // must not crash / free the active table
    h.renderMono(fe::makeNoise(2048, 4u));
}

TEST_CASE("96 kHz prepare: sizes follow the fs tier and the stream stays healthy",
          "[engine]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Original;
    h.params.cutoffHz = 1000.0f;
    h.params.mix = 0.8f;
    h.engine.setWavetable(fe::morphTable());
    h.prepare(96000.0, 512, 2);
    REQUIRE(h.engine.latencySamples() == 2048); // L = 4096 at 96 kHz

    auto out = h.renderMono(fe::makeNoise(48000, 9u, 0.4f)); // 0.5 s
    CHECK(fe::allFinite(out));
    CHECK(fe::rms(std::span<const float>(out).subspan(8192)) > 1e-4);
    ftc::ResponseCurve rc;
    CHECK(h.engine.readResponseCurve(rc));
}

TEST_CASE("oversized host blocks are sliced internally and match the block-by-block render",
          "[engine]") {
    ftc::Parameters p;
    p.mode = PhaseMode::Minimum;
    p.cutoffHz = 1500.0f;
    p.mix = 0.8f;

    const auto x = fe::makeNoise(8192, 55u, 0.4f);

    fe::Harness a; // honest host: 512-sample blocks
    a.params = p;
    a.engine.setWavetable(fe::morphTable());
    a.prepare(fe::kFs, 512, 2);
    const auto o1 = a.renderMono(x, 512);

    fe::Harness b; // rogue host: hands 8192 samples to an engine prepared for 512
    b.params = p;
    b.engine.setWavetable(fe::morphTable());
    b.prepare(fe::kFs, 512, 2);
    const auto o2 = b.renderMono(x, 8192);

    REQUIRE(o1.size() == o2.size());
    CHECK(std::memcmp(o1.data(), o2.data(), o1.size() * sizeof(float)) == 0);
}

TEST_CASE("reset clears stream state but keeps kernels: post-reset render is bit-identical",
          "[engine]") {
    ftc::Parameters p;
    p.mode = PhaseMode::Linear;
    p.cutoffHz = 1200.0f;
    p.resonance = 0.4f;
    p.mix = 0.7f;

    const auto x = fe::makeNoise(16384, 99u);

    fe::Harness a;
    a.params = p;
    a.engine.setWavetable(fe::morphTable());
    a.prepare();
    const auto o1 = a.renderMono(x);

    fe::Harness b;
    b.params = p;
    b.engine.setWavetable(fe::morphTable());
    b.prepare();
    b.renderMono(fe::makeNoise(16384, 123u)); // different history
    b.engine.reset();
    const auto o2 = b.renderMono(x);

    REQUIRE(o1.size() == o2.size());
    CHECK(std::memcmp(o1.data(), o2.data(), o1.size() * sizeof(float)) == 0);
}
