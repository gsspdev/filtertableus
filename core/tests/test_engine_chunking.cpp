// Engine acceptance: chunking invariance (bit-exact) and render determinism.
//
// The engine keys every control decision (mod evaluation, smoothers, ramp retargets, kernel
// ticks, crossfade starts) to an ABSOLUTE stream position, so the same sample stream chopped
// into different host block sizes must produce bit-identical output. Parameter automation in
// this test changes values only at positions that are block boundaries in BOTH runs (the
// engine consumes parameters per block, so automation granularity is necessarily a shared
// boundary grid — 2048 here). Internal LFO/env depths stay 0: their VALUES are evaluated on
// block-relative state whose floating-point accumulation is legitimately chopping-dependent,
// but with zero depth they contribute exactly 0.0f, keeping the audio path invariant.
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <vector>

#include "ftc/FilterTableEngine.h"
#include "test_engine_utils.h"

using ftc::PhaseMode;
namespace fe = ftet;

namespace {

constexpr int kAutoGrid = 2048;
constexpr int kN = kAutoGrid * 24; // 49152 samples ~ 1 s

ftc::Parameters autoParams(PhaseMode mode, int blockStart) {
    const float t = static_cast<float>(blockStart) / static_cast<float>(kN);
    ftc::Parameters p;
    p.mode = mode;
    p.scan = t;                                              // 0 -> 1 sweep
    p.cutoffHz = 200.0f * std::pow(40.0f, t);                // 200 Hz -> 8 kHz log sweep
    p.resonance = 0.5f * t;
    p.mix = 0.8f;
    p.outGainDb = -1.5f;
    return p;
}

/// Render with a repeating block-size pattern, splitting blocks at kAutoGrid boundaries so
/// both runs see identical parameter values at identical stream positions.
std::vector<float> renderPattern(PhaseMode mode, const std::vector<float>& input,
                                 std::span<const int> pattern) {
    fe::Harness h;
    h.params = autoParams(mode, 0);
    h.engine.setWavetable(fe::morphTable());
    h.prepare(fe::kFs, 4096, 2);

    std::vector<std::vector<float>> bufs{input, input};
    int pos = 0;
    size_t pi = 0;
    while (pos < kN) {
        int n = pattern[pi % pattern.size()];
        ++pi;
        n = std::min(n, kN - pos);
        const int toGrid = kAutoGrid - pos % kAutoGrid; // never straddle automation points
        n = std::min(n, toGrid);
        h.params = autoParams(mode, (pos / kAutoGrid) * kAutoGrid);
        h.block(bufs, static_cast<size_t>(pos), n);
        pos += n;
    }
    return bufs[0];
}

} // namespace

TEST_CASE("chunking invariance: {512}xN vs irregular block sizes is bit-exact, all modes",
          "[engine][chunking]") {
    const auto input = fe::makeNoise(kN, 2024u, 0.5f);
    const int uniform[] = {512};
    const int irregular[] = {1, 17, 64, 441, 4096, 512, 3, 128, 1024, 37, 2048, 5};

    for (PhaseMode mode : {PhaseMode::Minimum, PhaseMode::Linear, PhaseMode::Original,
                           PhaseMode::Raw}) {
        const auto a = renderPattern(mode, input, uniform);
        const auto b = renderPattern(mode, input, irregular);
        REQUIRE(a.size() == b.size());
        const bool identical = std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
        INFO("mode " << static_cast<int>(mode));
        CHECK(identical);
        CHECK(fe::allFinite(a));
    }
}

TEST_CASE("determinism: two identical full-feature renders are bit-identical", "[engine]") {
    constexpr int kLen = 24576;
    const auto input = fe::makeNoise(kLen, 777u, 0.5f);

    auto render = [&input]() {
        fe::Harness h;
        h.params.mode = PhaseMode::Minimum;
        h.params.cutoffHz = 800.0f;
        h.params.resonance = 0.4f;
        h.params.mix = 0.8f;
        h.params.outGainDb = -3.0f;
        h.params.keytrack = 0.5f;
        h.params.lfo1 = {ftc::LfoShape::Sine, false, 3.0f, ftc::SyncDivision::Quarter, true,
                         0.3f, 0.2f};
        h.params.lfo2 = {ftc::LfoShape::SampleHold, true, 1.0f, ftc::SyncDivision::Eighth,
                         false, 0.0f, 0.4f};
        h.params.env.sensitivityDb = 6.0f;
        h.params.env.attackMs = 5.0f;
        h.params.env.releaseMs = 80.0f;
        h.params.env.toScan = -0.2f;
        h.engine.setWavetable(fe::morphTable());
        h.prepare();

        std::vector<std::vector<float>> bufs{input, input};
        const double bps = 120.0 / 60.0 / fe::kFs; // beats per sample at 120 bpm
        for (int b = 0; b * 512 < kLen; ++b) {
            const int start = b * 512;
            ftc::TransportInfo t;
            t.valid = true;
            t.playing = true;
            t.bpm = 120.0;
            t.ppqPosition = static_cast<double>(start) * bps;
            std::vector<ftc::NoteEvent> notes;
            if (b == 3)
                notes.push_back({5, 64, 100, true});
            if (b == 30)
                notes.push_back({17, 64, 0, false});
            if (b == 24)
                h.params.mode = PhaseMode::Original; // mid-render mode switch
            h.params.scan = 0.5f * static_cast<float>(start) / static_cast<float>(kLen);
            h.block(bufs, static_cast<size_t>(start), std::min(512, kLen - start), t, notes);
        }
        return bufs[0];
    };

    const auto r1 = render();
    const auto r2 = render();
    REQUIRE(r1.size() == r2.size());
    CHECK(std::memcmp(r1.data(), r2.data(), r1.size() * sizeof(float)) == 0);
    CHECK(fe::allFinite(r1));
}
