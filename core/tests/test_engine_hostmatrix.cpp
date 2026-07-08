// Host-matrix coverage (Wave-3.1): engine health across the sample rates real hosts run at
// beyond the 48k/96k already covered — 44.1k (CD family), 88.2k and 176.4k (the ">100k" tier).
// For Minimum and Linear at each rate: prepare, render noise, output finite and live, and the
// reported latency equals latencySamplesFor(mode, fs) before AND after the render.
#include <catch2/catch_test_macros.hpp>

#include <span>
#include <vector>

#include "ftc/FilterTableEngine.h"
#include "test_engine_utils.h"

using ftc::FilterTableEngine;
using ftc::PhaseMode;
namespace fe = ftet;

TEST_CASE("sample-rate matrix: Minimum/Linear render finite with exact reported latency",
          "[engine][hostmatrix]") {
    const auto table = fe::morphTable();
    for (double fs : {44100.0, 88200.0, 176400.0}) {
        for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear}) {
            fe::Harness h;
            h.params.mode = mode;
            h.params.cutoffHz = 2000.0f;
            h.params.mix = 0.8f;
            h.engine.setWavetable(table);
            h.prepare(fs, 512, 2);

            const int expected = FilterTableEngine::latencySamplesFor(mode, fs);
            INFO("fs=" << fs << " mode=" << static_cast<int>(mode)
                       << " expected latency=" << expected);
            REQUIRE(h.engine.latencySamples() == expected);

            // ~0.25 s of noise: covers the kernel support + ramps at every tier.
            const int n = static_cast<int>(fs * 0.25);
            const auto out = h.renderMono(fe::makeNoise(n, 17u, 0.4f));
            REQUIRE(fe::allFinite(out));
            // Live output well past the latency fill (not stuck silent/idle).
            CHECK(fe::rms(std::span<const float>(out).subspan(static_cast<size_t>(n / 2)))
                  > 1e-4);
            // Latency must not have moved with streaming (only mode switches may move it).
            CHECK(h.engine.latencySamples() == expected);
        }
    }
}
