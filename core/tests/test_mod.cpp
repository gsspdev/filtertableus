// ModulationEngine acceptance tests (JUCE-free): LFO shapes/periods/sync/timeline lock,
// envelope follower attack/release/sensitivity, keytrack + note tracker, retrigger, S&H.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <vector>

#include "ftc/Modulation.h"
#include "ftc/Parameters.h"
#include "ftc/Types.h"

using Catch::Approx;

namespace {

constexpr double kFs = 48000.0;
constexpr int kTick = 64;

struct TickSample {
    long t = 0; // global sample time of the evaluate call
    ftc::ModValues v{};
};

/// Drives the engine block by block, evaluating at every control tick.
struct ModHarness {
    ftc::ModulationEngine eng;
    ftc::Parameters p{};
    long totalSamples = 0;
    ftc::TransportInfo transport{};

    ModHarness() { eng.prepare(kFs, kTick); }

    std::vector<TickSample> block(int n, std::span<const ftc::NoteEvent> notes = {},
                                  const float* mono = nullptr) {
        eng.setParams(p);
        eng.beginBlock(transport, notes, mono, n);
        std::vector<TickSample> out;
        for (int o = 0; o < n; o += kTick)
            out.push_back({totalSamples + o, eng.evaluate(o)});
        totalSamples += n;
        if (transport.valid && transport.playing)
            transport.ppqPosition += static_cast<double>(n) * transport.bpm / (60.0 * kFs);
        return out;
    }

    std::vector<TickSample> run(long numSamples, int blockSize = 512) {
        std::vector<TickSample> all;
        long done = 0;
        while (done < numSamples) {
            const int n = static_cast<int>(std::min<long>(blockSize, numSamples - done));
            auto b = block(n);
            all.insert(all.end(), b.begin(), b.end());
            done += n;
        }
        return all;
    }
};

/// Sample times where a saw-up LFO wraps (value falls by more than 1).
std::vector<long> wrapTimes(const std::vector<TickSample>& s) {
    std::vector<long> w;
    for (size_t i = 1; i < s.size(); ++i)
        if (s[i].v.scanOffset < s[i - 1].v.scanOffset - 1.0f)
            w.push_back(s[i].t);
    return w;
}

double meanPeriod(const std::vector<long>& wraps) {
    REQUIRE(wraps.size() >= 2);
    return static_cast<double>(wraps.back() - wraps.front()) /
           static_cast<double>(wraps.size() - 1);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// LFO shapes
// ---------------------------------------------------------------------------------------------

TEST_CASE("each LFO shape hits its expected extrema", "[mod][lfo]") {
    struct Case {
        ftc::LfoShape shape;
        float lo, hi, tol;
    };
    const Case cases[] = {
        {ftc::LfoShape::Sine, -1.0f, 1.0f, 0.01f},
        {ftc::LfoShape::Triangle, -1.0f, 1.0f, 0.05f},
        {ftc::LfoShape::SawUp, -1.0f, 1.0f, 0.02f},
        {ftc::LfoShape::SawDown, -1.0f, 1.0f, 0.02f},
        {ftc::LfoShape::Square, -1.0f, 1.0f, 0.0f},
    };
    for (const auto& c : cases) {
        ModHarness h;
        h.p.lfo1.shape = c.shape;
        h.p.lfo1.rateHz = 2.0f;
        h.p.lfo1.toScan = 1.0f;
        auto s = h.run(72000); // 1.5 s = 3 cycles
        float mn = 10.0f, mx = -10.0f;
        for (const auto& x : s) {
            mn = std::min(mn, x.v.scanOffset);
            mx = std::max(mx, x.v.scanOffset);
        }
        INFO("shape " << static_cast<int>(c.shape));
        REQUIRE(mn <= c.lo + c.tol);
        REQUIRE(mn >= c.lo - 1e-4f);
        REQUIRE(mx >= c.hi - c.tol);
        REQUIRE(mx <= c.hi + 1e-4f);
    }
}

TEST_CASE("S&H stays in [-1,1], holds within a cycle, changes across cycles", "[mod][lfo][sah]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SampleHold;
    h.p.lfo1.rateHz = 10.0f; // 4800-sample cycles = 75 ticks
    h.p.lfo1.toScan = 1.0f;
    auto s = h.run(48000);
    int changes = 0;
    std::vector<float> distinct;
    for (size_t i = 0; i < s.size(); ++i) {
        REQUIRE(s[i].v.scanOffset >= -1.0f);
        REQUIRE(s[i].v.scanOffset <= 1.0f);
        if (i > 0 && s[i].v.scanOffset != s[i - 1].v.scanOffset)
            ++changes;
    }
    // 10 Hz for 1 s -> ~10 new values (draw boundaries quantized to ticks)
    REQUIRE(changes >= 8);
    REQUIRE(changes <= 12);
}

TEST_CASE("S&H is deterministic: identical runs and reset() reproduce the sequence",
          "[mod][lfo][sah]") {
    auto capture = [](ftc::ModulationEngine& eng) {
        ftc::Parameters p{};
        p.lfo1.shape = ftc::LfoShape::SampleHold;
        p.lfo1.rateHz = 20.0f;
        p.lfo1.toScan = 1.0f;
        std::vector<float> out;
        for (int b = 0; b < 40; ++b) {
            eng.setParams(p);
            eng.beginBlock({}, {}, nullptr, 512);
            for (int o = 0; o < 512; o += kTick)
                out.push_back(eng.evaluate(o).scanOffset);
        }
        return out;
    };
    ftc::ModulationEngine a, b;
    a.prepare(kFs, kTick);
    b.prepare(kFs, kTick);
    const auto ra = capture(a);
    const auto rb = capture(b);
    REQUIRE(ra == rb);
    a.reset();
    const auto rc = capture(a);
    REQUIRE(ra == rc);
}

// ---------------------------------------------------------------------------------------------
// Periods (free + synced)
// ---------------------------------------------------------------------------------------------

TEST_CASE("free-running 1 Hz LFO period at 48 kHz is within 0.5%", "[mod][lfo][period]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.rateHz = 1.0f;
    h.p.lfo1.toScan = 1.0f;
    auto s = h.run(48000 * 5);
    const double period = meanPeriod(wrapTimes(s));
    REQUIRE(period == Approx(48000.0).epsilon(0.005));
}

TEST_CASE("synced 1/4 at 120 BPM has a 0.5 s period", "[mod][lfo][sync]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.tempoSync = true;
    h.p.lfo1.division = ftc::SyncDivision::Quarter;
    h.p.lfo1.toScan = 1.0f;
    h.transport = {120.0, 0.0, true, true};
    auto s = h.run(48000 * 3);
    const double period = meanPeriod(wrapTimes(s));
    REQUIRE(period == Approx(24000.0).epsilon(0.005));
}

TEST_CASE("synced LFO free-runs from the current BPM when stopped", "[mod][lfo][sync]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.tempoSync = true;
    h.p.lfo1.division = ftc::SyncDivision::Half; // 2 beats -> 1 s at 120 BPM
    h.p.lfo1.toScan = 1.0f;
    h.transport = {120.0, 0.0, false, true}; // valid but stopped
    auto s = h.run(48000 * 5);
    const double period = meanPeriod(wrapTimes(s));
    REQUIRE(period == Approx(48000.0).epsilon(0.005));
}

// ---------------------------------------------------------------------------------------------
// Phase continuity & timeline lock
// ---------------------------------------------------------------------------------------------

TEST_CASE("free-running phase is continuous across evaluate calls and odd block sizes",
          "[mod][lfo][continuity]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.rateHz = 1.0f;
    h.p.lfo1.toScan = 1.0f;
    auto s = h.run(40000, 480); // 480 is not a multiple of 64: exercises the tail advance
    for (const auto& x : s) {
        const double expected = 2.0 * (static_cast<double>(x.t) / 48000.0) - 1.0;
        REQUIRE(static_cast<double>(x.v.scanOffset) == Approx(expected).margin(1e-6));
    }
}

TEST_CASE("timeline lock: ppq 3.0 equals ppq 7.0 for a one-bar cycle", "[mod][lfo][timeline]") {
    ftc::Parameters p{};
    p.lfo1.shape = ftc::LfoShape::Sine;
    p.lfo1.tempoSync = true;
    p.lfo1.division = ftc::SyncDivision::W1; // 4 beats = 1 bar in 4/4
    p.lfo1.toScan = 1.0f;

    // Same engine, loop jump 3.0 -> 7.0 (one bar later).
    ftc::ModulationEngine eng;
    eng.prepare(kFs, kTick);
    eng.setParams(p);
    eng.beginBlock({120.0, 3.0, true, true}, {}, nullptr, 512);
    const float at3 = eng.evaluate(0).scanOffset;
    eng.setParams(p);
    eng.beginBlock({120.0, 7.0, true, true}, {}, nullptr, 512);
    const float at7 = eng.evaluate(0).scanOffset;
    REQUIRE(at3 == Approx(at7).margin(1e-6));

    // And a fresh engine started directly at 7.0 agrees (phase is a pure transport function).
    ftc::ModulationEngine eng2;
    eng2.prepare(kFs, kTick);
    eng2.setParams(p);
    eng2.beginBlock({120.0, 7.0, true, true}, {}, nullptr, 512);
    REQUIRE(eng2.evaluate(0).scanOffset == Approx(at3).margin(1e-6));

    // Value is what the phase says it should be: frac(3/4) = 0.75 -> sin(2*pi*0.75) = -1.
    REQUIRE(at3 == Approx(-1.0).margin(1e-4));
}

TEST_CASE("playing S&H repeats identical values when the timeline loops", "[mod][lfo][sah]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SampleHold;
    h.p.lfo1.tempoSync = true;
    h.p.lfo1.division = ftc::SyncDivision::Eighth;
    h.p.lfo1.toScan = 1.0f;
    h.transport = {120.0, 0.0, true, true};
    auto pass1 = h.block(4096);
    h.transport.ppqPosition = 0.0; // host loops back
    auto pass2 = h.block(4096);
    REQUIRE(pass1.size() == pass2.size());
    for (size_t i = 0; i < pass1.size(); ++i)
        REQUIRE(pass1[i].v.scanOffset == pass2[i].v.scanOffset);
}

// ---------------------------------------------------------------------------------------------
// Retrigger
// ---------------------------------------------------------------------------------------------

TEST_CASE("retrigger resets LFO phase on note-on; without it notes are ignored",
          "[mod][lfo][retrigger]") {
    for (const bool retrig : {true, false}) {
        ModHarness h;
        h.p.lfo1.shape = ftc::LfoShape::SawUp;
        h.p.lfo1.rateHz = 1.0f;
        h.p.lfo1.toScan = 1.0f;
        h.p.lfo1.retrigger = retrig;
        h.block(512); // advance some phase first
        const ftc::NoteEvent note{100, 60, 100, true};
        auto s = h.block(512, std::span<const ftc::NoteEvent>(&note, 1));
        const float at128 = s[2].v.scanOffset; // evaluate(128), 28 samples after the note
        if (retrig)
            REQUIRE(static_cast<double>(at128) ==
                    Approx(-1.0 + 2.0 * 28.0 / 48000.0).margin(1e-6));
        else
            REQUIRE(static_cast<double>(at128) ==
                    Approx(-1.0 + 2.0 * 640.0 / 48000.0).margin(1e-6));
    }
}

TEST_CASE("retrigger re-anchors a synced LFO while playing", "[mod][lfo][retrigger]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.tempoSync = true;
    h.p.lfo1.division = ftc::SyncDivision::Quarter;
    h.p.lfo1.toScan = 1.0f;
    h.p.lfo1.retrigger = true;
    h.transport = {120.0, 10.25, true, true}; // mid-beat: phase would be 0.25 without retrigger
    const ftc::NoteEvent note{0, 60, 100, true};
    auto s = h.block(512, std::span<const ftc::NoteEvent>(&note, 1));
    // Phase restarted at the note: value at evaluate(0) is the saw start (-1).
    REQUIRE(static_cast<double>(s[0].v.scanOffset) == Approx(-1.0).margin(1e-6));
}

// ---------------------------------------------------------------------------------------------
// Envelope follower
// ---------------------------------------------------------------------------------------------

TEST_CASE("envelope step response reaches 1-1/e of final at attackMs", "[mod][env]") {
    ModHarness h;
    h.p.env.attackMs = 40.0f; // 1920 samples at 48 kHz = 30 control ticks exactly
    h.p.env.releaseMs = 200.0f;
    h.p.env.toScan = 1.0f;
    std::vector<float> ones(2048, 1.0f);
    h.eng.setParams(h.p);
    h.eng.beginBlock({}, {}, ones.data(), static_cast<int>(ones.size()));
    const float atTau = h.eng.evaluate(1920).scanOffset;
    REQUIRE(static_cast<double>(atTau) == Approx(1.0 - 1.0 / M_E).epsilon(0.10));
}

TEST_CASE("envelope release is symmetric: decays to 1/e of start at releaseMs", "[mod][env]") {
    ModHarness h;
    h.p.env.attackMs = 1.0f;
    h.p.env.releaseMs = 100.0f; // 4800 samples = 75 ticks
    h.p.env.toScan = 1.0f;
    std::vector<float> ones(24000, 1.0f);
    h.eng.setParams(h.p);
    h.eng.beginBlock({}, {}, ones.data(), static_cast<int>(ones.size()));
    h.eng.evaluate(0);
    const float charged = h.eng.envValue();
    REQUIRE(static_cast<double>(charged) == Approx(1.0).margin(1e-3));

    std::vector<float> zeros(9600, 0.0f);
    h.eng.setParams(h.p);
    h.eng.beginBlock({}, {}, zeros.data(), static_cast<int>(zeros.size()));
    const float atTau = h.eng.evaluate(4800).scanOffset;
    REQUIRE(static_cast<double>(atTau) ==
            Approx(static_cast<double>(charged) / M_E).epsilon(0.10));
}

TEST_CASE("sensitivity dB scales the follower input; output clamps to 0..1", "[mod][env]") {
    auto steadyState = [](float sensDb, float input) {
        ModHarness h;
        h.p.env.sensitivityDb = sensDb;
        h.p.env.attackMs = 10.0f;
        h.p.env.releaseMs = 200.0f;
        std::vector<float> sig(48000, input);
        for (int b = 0; b < 2; ++b) {
            h.eng.setParams(h.p);
            h.eng.beginBlock({}, {}, sig.data(), static_cast<int>(sig.size()));
            h.eng.evaluate(0);
        }
        return h.eng.envValue();
    };
    REQUIRE(static_cast<double>(steadyState(0.0f, 0.05f)) == Approx(0.05).epsilon(0.05));
    REQUIRE(static_cast<double>(steadyState(12.0f, 0.05f)) ==
            Approx(0.05 * std::pow(10.0, 12.0 / 20.0)).epsilon(0.05));
    REQUIRE(static_cast<double>(steadyState(-12.0f, 0.05f)) ==
            Approx(0.05 * std::pow(10.0, -12.0 / 20.0)).epsilon(0.05));
    // +24 dB on 0.5 would be ~7.9: output must clamp to 1.
    REQUIRE(steadyState(24.0f, 0.5f) == 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Keytrack / note tracker
// ---------------------------------------------------------------------------------------------

TEST_CASE("keytrack: note 81 with keytrack 1 gives +12 st; -0.5 gives -6", "[mod][keytrack]") {
    for (const auto& [kt, expected] : {std::pair{1.0f, 12.0f}, std::pair{-0.5f, -6.0f}}) {
        ModHarness h;
        h.p.keytrack = kt;
        REQUIRE(h.block(512)[0].v.cutoffSemis == 0.0f); // A4 default: neutral before any note
        const ftc::NoteEvent note{0, 81, 100, true};
        auto s = h.block(512, std::span<const ftc::NoteEvent>(&note, 1));
        REQUIRE(s[0].v.cutoffSemis == Approx(expected).margin(1e-5));
        REQUIRE(s.back().v.cutoffSemis == Approx(expected).margin(1e-5));
    }
}

TEST_CASE("note tracker: last-note priority, revert on release, hold on all-off",
          "[mod][keytrack]") {
    ModHarness h;
    h.p.keytrack = 1.0f;
    const ftc::NoteEvent on81{0, 81, 100, true};
    REQUIRE(h.block(256, {&on81, 1})[0].v.cutoffSemis == Approx(12.0f));
    const ftc::NoteEvent on60{0, 60, 100, true};
    REQUIRE(h.block(256, {&on60, 1})[0].v.cutoffSemis == Approx(-9.0f)); // last-note priority
    const ftc::NoteEvent off60{0, 60, 0, false};
    REQUIRE(h.block(256, {&off60, 1})[0].v.cutoffSemis == Approx(12.0f)); // revert to held 81
    const ftc::NoteEvent off81{0, 81, 0, false};
    REQUIRE(h.block(256, {&off81, 1})[0].v.cutoffSemis == Approx(12.0f)); // all off: hold
}

TEST_CASE("note events are applied in offset order within a block", "[mod][keytrack]") {
    ModHarness h;
    h.p.keytrack = 1.0f;
    const ftc::NoteEvent notes[] = {{0, 69, 100, true}, {192, 81, 100, true}};
    auto s = h.block(512, notes);
    REQUIRE(s[0].v.cutoffSemis == Approx(0.0f).margin(1e-6));  // note 69 at offset 0
    REQUIRE(s[2].v.cutoffSemis == Approx(0.0f).margin(1e-6));  // offset 128: still 69
    REQUIRE(s[3].v.cutoffSemis == Approx(12.0f).margin(1e-6)); // offset 192: note 81
}

// ---------------------------------------------------------------------------------------------
// Summing
// ---------------------------------------------------------------------------------------------

TEST_CASE("evaluate sums LFO/env/keytrack per the ModValues contract", "[mod][sum]") {
    ModHarness h;
    h.p.lfo1.shape = ftc::LfoShape::SawUp;
    h.p.lfo1.tempoSync = true;
    h.p.lfo1.division = ftc::SyncDivision::Quarter;
    h.p.lfo1.toScan = 0.5f;
    h.p.lfo1.toCutoff = 1.0f;
    h.p.keytrack = 1.0f;
    h.transport = {120.0, 0.25, true, true}; // phase 0.25 -> sawUp = -0.5 exactly
    const ftc::NoteEvent note{0, 81, 100, true};
    auto s = h.block(512, std::span<const ftc::NoteEvent>(&note, 1));
    REQUIRE(static_cast<double>(s[0].v.scanOffset) == Approx(-0.25).margin(1e-6));
    // 48 * (-0.5 * 1.0) + 1.0 * (81 - 69) = -24 + 12 = -12
    REQUIRE(static_cast<double>(s[0].v.cutoffSemis) == Approx(-12.0).margin(1e-4));
}

TEST_CASE("default parameters yield zero modulation", "[mod][sum]") {
    ModHarness h;
    auto s = h.run(4096);
    for (const auto& x : s) {
        REQUIRE(x.v.scanOffset == 0.0f);
        REQUIRE(x.v.cutoffSemis == 0.0f);
    }
}
