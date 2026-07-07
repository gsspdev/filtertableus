// Wave-3 integration regressions (checklist items 1, 2 and the end-to-end part of item 8):
//   - prepareToPlay pushes the parameter snapshot into the engine BEFORE prepare(), so a
//     session restored to Linear/Original reports its L/2 latency immediately.
//   - Parameter bypass (and processBlockBypassed) run through the engine's latency-matched
//     dry path: bypassed audio is exactly the input delayed by the REPORTED latency.
//   - DAW-less sanity through the real processor: scan sweep on a saw in every phase mode
//     stays finite/bounded/non-silent with constant latency; LINEAR mode nulls against the
//     latency-aligned dry signal at mix 50% end-to-end (post Wave-3 decision:
//     FTUS_LINEAR_HALF_SAMPLE_CENTER=0, integer kernel center).
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <vector>

#include <juce_data_structures/juce_data_structures.h>

#include "IntegrationTestHelpers.h"
#include "ftc/EngineConfig.h"

using namespace itest;

namespace {

constexpr double kFs = 48000.0;
constexpr int kL = 2048; // ftc::EngineConfig::kernelLength(48000)

/// Single impulse frame: |FFT| == 1 in every bin — the flattest possible table, so the
/// spectral path at wide-open cutoff is a pure (possibly delayed) passthrough.
ftc::WavetablePtr impulseTable() {
    std::vector<float> f(static_cast<size_t>(ftc::WavetableData::kFrameLength), 0.0f);
    f[0] = 1.0f;
    return ftc::WavetableData::analyze(f, 1, "flat-impulse");
}

/// Naive band-unlimited saw at `freq` on every channel (deterministic test signal).
void fillSaw(juce::AudioBuffer<float>& buf, double sampleRate, double freq, float amp = 0.4f) {
    float* d0 = buf.getWritePointer(0);
    for (int n = 0; n < buf.getNumSamples(); ++n) {
        const double ph = std::fmod(freq * n / sampleRate, 1.0);
        d0[n] = amp * static_cast<float>(2.0 * ph - 1.0);
    }
    for (int ch = 1; ch < buf.getNumChannels(); ++ch)
        buf.copyFrom(ch, 0, buf, 0, 0, buf.getNumSamples());
}

/// RMS (dBFS) of channel 0 over [start, start+len).
float windowRmsDb(const juce::AudioBuffer<float>& buf, int start, int len) {
    return rmsDb(buf, 0, start, len);
}

} // namespace

// ---------------------------------------------------------------------- item 1: prepare order
TEST_CASE_METHOD(JuceEnv, "w3: prepareToPlay reports the restored mode's latency immediately",
                 "[wave3][latency]") {
    struct Case {
        int mode;
        int expected;
    };
    for (const auto c : {Case{0, 0}, Case{1, kL / 2}, Case{2, kL / 2}, Case{3, 0}}) {
        ftus::FtusAudioProcessor proc;
        setChoiceParam(proc, ftus::ids::phaseMode, c.mode);
        proc.setPlayConfigDetails(2, 2, kFs, 512);
        proc.prepareToPlay(kFs, 512);
        // No block has been processed yet: the host must already see the right latency.
        INFO("mode index " << c.mode);
        CHECK(proc.getLatencySamples() == c.expected);
        CHECK(proc.engine().latencySamples() == c.expected);
    }
}

// -------------------------------------------------------------------- item 2: bypass alignment
TEST_CASE_METHOD(JuceEnv, "w3: parameter bypass is time-aligned via the dry delay",
                 "[wave3][bypass]") {
    struct Case {
        int mode;
        int delay;
    };
    for (const auto c : {Case{1, kL / 2}, Case{2, kL / 2}, Case{0, 0}, Case{3, 0}}) {
        ftus::FtusAudioProcessor proc;
        setChoiceParam(proc, ftus::ids::phaseMode, c.mode);
        setParamReal(proc, ftus::ids::mix, 1.0f);
        setParamReal(proc, ftus::ids::outGain, 6.0f); // must be neutralized while bypassed
        setBoolParam(proc, ftus::ids::bypass, true);
        proc.setPlayConfigDetails(2, 2, kFs, 512);
        proc.prepareToPlay(kFs, 512);
        // Strongly filtering table: if bypass leaked through the wet path, the null would
        // fail loudly.
        proc.adoptWavetable(makeFilterTable("BypassProbe", {8}), userTableInfo("BypassProbe"));

        juce::AudioBuffer<float> in(2, 48000);
        fillNoise(in, 0xB19A55u);
        RenderPlan plan;
        const auto res = renderThroughProcessor(proc, in, plan);
        REQUIRE(allFinite(res.output));
        CHECK(res.hostLatencyConstant);
        CHECK(res.hostLatencyAtStart == c.delay);

        // After the 10 ms bypass ramps settle (warmup blocks are silent, then we skip 4096
        // samples), bypassed output must equal the input delayed by the REPORTED latency.
        const float depth = nullDepthDb(res.output, in, c.delay, 4096);
        INFO("mode index " << c.mode << ", null depth vs delayed dry = " << depth << " dB");
        CHECK(depth < -100.0f);
    }
}

TEST_CASE_METHOD(JuceEnv, "w3: processBlockBypassed uses the same latency-matched dry path",
                 "[wave3][bypass]") {
    ftus::FtusAudioProcessor proc;
    setChoiceParam(proc, ftus::ids::phaseMode, 1); // Linear
    setParamReal(proc, ftus::ids::mix, 1.0f);
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    proc.prepareToPlay(kFs, 512);
    proc.adoptWavetable(makeFilterTable("BypassProbe2", {8}), userTableInfo("BypassProbe2"));

    juce::AudioBuffer<float> in(2, 48000);
    fillNoise(in, 0x0DDBA11u);

    juce::AudioBuffer<float> out(2, in.getNumSamples());
    juce::AudioBuffer<float> block(2, 512);
    juce::MidiBuffer midi;
    for (int w = 0; w < 4; ++w) { // settle the forced-bypass ramps over silence
        block.clear();
        midi.clear();
        proc.processBlockBypassed(block, midi);
    }
    for (int off = 0; off < in.getNumSamples(); off += 512) {
        const int n = std::min(512, in.getNumSamples() - off);
        block.setSize(2, n, false, false, true);
        for (int ch = 0; ch < 2; ++ch)
            block.copyFrom(ch, 0, in, ch, off, n);
        midi.clear();
        proc.processBlockBypassed(block, midi);
        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom(ch, off, block, ch, 0, n);
    }
    REQUIRE(allFinite(out));
    const float depth = nullDepthDb(out, in, kL / 2, 4096);
    INFO("processBlockBypassed null depth vs L/2-delayed dry = " << depth << " dB");
    CHECK(depth < -100.0f);
}

// ------------------------------------------------------- item 8: end-to-end phase-mode sweeps
TEST_CASE_METHOD(JuceEnv, "w3: scan sweep on a saw stays clean in every phase mode",
                 "[wave3][sweep]") {
    for (int mode = 0; mode < 4; ++mode) {
        ftus::FtusAudioProcessor proc;
        setChoiceParam(proc, ftus::ids::phaseMode, mode);
        setParamReal(proc, ftus::ids::mix, 1.0f);
        setParamReal(proc, ftus::ids::cutoff, 2000.0f);
        proc.setPlayConfigDetails(2, 2, kFs, 512);
        proc.prepareToPlay(kFs, 512);
        proc.adoptWavetable(makeFilterTable("SweepTable", {4, 16, 64, 256, 1024}),
                            userTableInfo("SweepTable"));

        juce::AudioBuffer<float> in(2, 3 * 48000);
        fillSaw(in, kFs, 110.0);

        RenderPlan plan;
        plan.scanRampMidRender = true; // scan 0 -> 1 between 25% and 75% of the render
        const auto res = renderThroughProcessor(proc, in, plan);

        REQUIRE(allFinite(res.output));
        CHECK(res.hostLatencyConstant);
        CHECK(res.engineLatencyConstant);
        const float peak = maxAbsSample(res.output);
        INFO("mode index " << mode << " peak " << peak);
        // Bounded, no runaway. Minimum/Linear/Original kernels are response-peak-normalized
        // to 0 dB; RAW is energy-normalized (Sum h^2 = 1) by contract, so narrow-band gains
        // may legitimately exceed 0 dB (measured peak 2.15 on the 0.4-peak saw).
        CHECK(peak < 4.0f);

        // No dropouts while the sweep runs: 100 ms RMS windows across the middle half of the
        // render (where the scan ramp is active) stay above -55 dBFS.
        const int win = 4800;
        float minRms = 0.0f;
        for (int start = in.getNumSamples() / 4; start + win <= 3 * in.getNumSamples() / 4;
             start += win)
            minRms = std::min(minRms, windowRmsDb(res.output, start, win));
        INFO("mode index " << mode << " min sweep-window RMS " << minRms << " dBFS");
        CHECK(minRms > -55.0f);
    }
}

// --------------------------------------------------------------- screenshot session seeding
// Hidden utility (run manually: ftus_integration_tests "[.seed-standalone]" [args via env]):
// writes a deterministic session (factory preset loaded + mid-scan) into the standalone's
// settings file so `open FilterTableUS.app` restores a fully-populated GUI (loaded table in
// the waterfall, real response curve in the spectrum, preset name in the bar) for the Wave-3
// final screenshots — no UI automation required. Env: FTUS_SEED_PRESET (default "Comb
// Runner"), FTUS_SEED_SCAN (default 0.45).
TEST_CASE_METHOD(JuceEnv, "w3 seed standalone session for screenshots (manual)",
                 "[.seed-standalone]") {
    const char* presetEnv = std::getenv("FTUS_SEED_PRESET");
    const juce::String presetName = presetEnv != nullptr && presetEnv[0] != '\0'
                                        ? juce::String::fromUTF8(presetEnv)
                                        : juce::String("Comb Runner");
    const char* scanEnv = std::getenv("FTUS_SEED_SCAN");
    const float scan = scanEnv != nullptr ? juce::String(scanEnv).getFloatValue() : 0.45f;

    ftus::FtusAudioProcessor proc;
    REQUIRE(proc.stateManager().loadPreset(presetName));
    setParamReal(proc, ftus::ids::scan, scan);

    juce::MemoryBlock blob;
    proc.getStateInformation(blob);

    // Same PropertiesFile the JUCE standalone shell uses (see juce_StandaloneFilterWindow.h).
    juce::PropertiesFile::Options opts;
    opts.applicationName = "FilterTableUS";
    opts.filenameSuffix = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    juce::PropertiesFile props(opts);
    props.setValue("filterState", blob.toBase64Encoding());
    REQUIRE(props.save());
    std::cout << "seeded standalone session: preset='" << presetName.toStdString()
              << "' scan=" << scan << " -> " << props.getFile().getFullPathName().toStdString()
              << std::endl;
}

TEST_CASE_METHOD(JuceEnv, "w3: LINEAR nulls against dry at mix 50% end-to-end",
                 "[wave3][null]") {
    ftus::FtusAudioProcessor proc;
    setChoiceParam(proc, ftus::ids::phaseMode, 1); // Linear
    setParamReal(proc, ftus::ids::mix, 0.5f);
    setParamReal(proc, ftus::ids::cutoff, 20000.0f);
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    proc.prepareToPlay(kFs, 512);
    proc.adoptWavetable(impulseTable(), userTableInfo("flat-impulse"));

    juce::AudioBuffer<float> in(2, 48000);
    fillNoise(in, 0x11FEA5u);
    RenderPlan plan;
    const auto res = renderThroughProcessor(proc, in, plan);
    REQUIRE(allFinite(res.output));
    REQUIRE(res.hostLatencyAtStart == kL / 2);

    // Wet (flat response, linear phase, integer center) == dry (delayed L/2): mixing 50/50
    // must reproduce the delayed input to well below -60 dB (Wave-3 decision, item 4).
    const float depth = nullDepthDb(res.output, in, kL / 2, 4096);
    INFO("end-to-end LINEAR 50% null depth = " << depth << " dB");
    CHECK(depth < -60.0f);
}
