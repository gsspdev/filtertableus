// Engine acceptance: LINEAR dry-null at 50% mix, MINIMUM flat-table null, dry-path alignment.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/EngineConfig.h"
#include "ftc/FilterTableEngine.h"
#include "test_engine_utils.h"

using ftc::PhaseMode;
namespace fe = ftet;

namespace {

constexpr int kN = 32768;
constexpr int kSettle = 4096; // skip initial latency fill + ramp settle when measuring

double residualDbfs(std::span<const float> out, std::span<const float> ref) {
    const auto r = fe::residual(out, ref);
    return fe::dbfs(fe::rms(std::span<const float>(r).subspan(kSettle)));
}

} // namespace

// With no table the Linear kernel is an exact unit impulse at L/2, so the wet path is an
// exact L/2 delay: mixing 50/50 with the engine's latency-aligned dry path must null against
// the input delayed by exactly L/2.
TEST_CASE("LINEAR null: mix 0.5 output nulls against input delayed L/2 (no table)",
          "[engine]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Linear;
    h.params.cutoffHz = 20000.0f;
    h.params.mix = 0.5f;
    h.prepare();

    const auto x = fe::makeNoise(kN, 11u, 0.5f);
    const auto out = h.renderMono(x);
    REQUIRE(fe::allFinite(out));
    const auto ref = fe::delayed(x, fe::kL / 2);
    const double db = residualDbfs(out, ref);
    INFO("residual = " << db << " dBFS");
    CHECK(db < -60.0);
}

// Pure dry path (mix 0) must be a bit-exact L/2 delay of the input — this pins the internal
// dry delay to the reported latency with no tolerance at all.
TEST_CASE("LINEAR mix 0: dry path is an exact L/2 delay", "[engine]") {
    fe::Harness h;
    h.params.mode = PhaseMode::Linear;
    h.params.cutoffHz = 20000.0f;
    h.params.mix = 0.0f;
    h.engine.setWavetable(fe::flatTable());
    h.prepare();

    const auto x = fe::makeNoise(kN, 12u, 0.5f);
    const auto out = h.renderMono(x);
    const auto ref = fe::delayed(x, fe::kL / 2);
    for (size_t i = 0; i < out.size(); ++i)
        REQUIRE(out[i] == ref[i]); // bit-exact
}

TEST_CASE("MINIMUM null: flat table nulls against the undelayed input", "[engine]") {
    for (float mix : {0.5f, 1.0f}) {
        fe::Harness h;
        h.params.mode = PhaseMode::Minimum;
        h.params.cutoffHz = 20000.0f;
        h.params.mix = mix;
        h.engine.setWavetable(fe::flatTable());
        h.prepare();
        REQUIRE(h.engine.latencySamples() == 0);

        const auto x = fe::makeNoise(kN, 13u, 0.5f);
        const auto out = h.renderMono(x);
        REQUIRE(fe::allFinite(out));
        const double db = residualDbfs(out, x);
        INFO("mix = " << mix << ", residual = " << db << " dBFS");
        CHECK(db < -60.0);
    }
}

// The flat-TABLE Linear null depends on A2's compile-time symmetry center:
//   FTUS_LINEAR_HALF_SAMPLE_CENTER=0 (DEFAULT since Wave-3): integer center at tap L/2 ->
//     the strict -60 dBFS broadband null must hold at 50% mix.
//   FTUS_LINEAR_HALF_SAMPLE_CENTER=1 (calibration variant): kernel symmetric about (L-1)/2 ->
//     the wet path carries a half-sample delay relative to the dry path and a broadband 50%
//     null is mathematically impossible (documented in the A2 report). We then assert the wet
//     path is the documented half-sample-delayed passthrough and check the comb against its
//     model.
// The probe below detects which variant is compiled in, so this test adapts automatically
// if calibration flips the switch back.
TEST_CASE("LINEAR null: flat table, mix 0.5 (adapts to A2 symmetry-center switch)",
          "[engine]") {
    // Probe the wet impulse response for the kernel center.
    fe::Harness probe;
    probe.params.mode = PhaseMode::Linear;
    probe.params.cutoffHz = 20000.0f;
    probe.params.mix = 1.0f;
    probe.engine.setWavetable(fe::flatTable());
    probe.prepare();
    std::vector<float> imp(static_cast<size_t>(fe::kL + 512), 0.0f);
    imp[0] = 1.0f;
    const auto ir = probe.renderMono(imp);
    const float atCenter = std::abs(ir[static_cast<size_t>(fe::kL / 2)]);
    const float atPrev = std::abs(ir[static_cast<size_t>(fe::kL / 2 - 1)]);
    const bool integerCentered = atCenter > 0.9f && atPrev < 0.1f;

    fe::Harness h;
    h.params.mode = PhaseMode::Linear;
    h.params.cutoffHz = 20000.0f;
    h.params.mix = 0.5f;
    h.engine.setWavetable(fe::flatTable());
    h.prepare();
    const auto x = fe::makeNoise(kN, 14u, 0.5f);
    const auto out = h.renderMono(x);
    REQUIRE(fe::allFinite(out));
    const double db = residualDbfs(out, fe::delayed(x, fe::kL / 2));

    if (integerCentered) {
        INFO("integer-centered Linear kernel; residual = " << db << " dBFS");
        CHECK(db < -60.0);
    } else {
        // Half-sample comb model: |residual(f)| = sin(pi f / (2 fs)) per unit input.
        // Over white noise: rms factor = sqrt(mean sin^2 over [0, fs/2]) = sqrt(1/2 - 1/pi).
        const double inRms = fe::rms(std::span<const float>(x).subspan(kSettle));
        const double model = fe::dbfs(inRms * std::sqrt(0.5 - 1.0 / 3.14159265358979));
        INFO("half-sample-centered Linear kernel (calibration variant): residual = "
             << db << " dBFS vs comb model " << model << " dBFS; the -60 dBFS broadband null "
             << "requires FTUS_LINEAR_HALF_SAMPLE_CENTER=0 (see final report)");
        CHECK(db < model + 3.0);  // matches the documented comb, no extra error
        CHECK(db > model - 12.0); // and is not mysteriously quieter than physics allows
        // The exact-null plumbing is still proven by the no-table and mix-0 tests above.
    }
}
