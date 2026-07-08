// Offline render suite through the REAL processor (no host).
//
// Tier A (always on, stub or real engine):
//   * 2 s exponential sweep (100 Hz -> 10 kHz) and 2 s white noise, per phase mode,
//     with a mid-render `scan` automation ramp and a mid-render state save/load cycle.
//   * output finite (no NaN/inf), per-channel RMS within [-80, +6] dBFS,
//     reported latency (host + engine instance) constant during the render.
//
// Tier B (gated BEHAVIORALLY on the real engine via itest::engineIsReal(); passes trivially
// with a printed notice under the passthrough stub — tightens automatically when the real
// engine lands):
//   * wet != dry through a strongly-filtering table (the filter actually filters)
//   * mix=0 output equals the input delayed by the reported instance latency (null < -60 dB)
//   * per-mode INSTANCE latency matches the static latencySamplesFor(mode, fs)
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.h"

using itest::JuceEnv;

namespace {

constexpr double kFs = 48000.0;
constexpr int kBlock = 512;
constexpr int kRenderSeconds = 2;
constexpr int kRenderSamples = static_cast<int>(kFs) * kRenderSeconds;

const char* modeName(int m) {
    static const char* names[] = {"Minimum", "Linear", "Original", "Raw"};
    return names[m];
}

/// Fresh processor prepared at 48k/512/stereo with the shared strongly-filtering table
/// adopted and the core parameters set BEFORE prepare (so block 1 already runs the mode).
std::unique_ptr<ftus::FtusAudioProcessor> makePreparedProcessor(int phaseModeIndex, float mix) {
    auto proc = std::make_unique<ftus::FtusAudioProcessor>();
    proc->setPlayConfigDetails(2, 2, kFs, kBlock);
    itest::setParamReal(*proc, ftus::ids::mix, mix);
    itest::setParamReal(*proc, ftus::ids::cutoff, 2000.0f);
    itest::setParamReal(*proc, ftus::ids::scan, 0.0f);
    itest::setParamReal(*proc, ftus::ids::resonance, 0.0f);
    itest::setParamReal(*proc, ftus::ids::outGain, 0.0f);
    itest::setChoiceParam(*proc, ftus::ids::phaseMode, phaseModeIndex);
    proc->prepareToPlay(kFs, kBlock);
    proc->adoptWavetable(itest::makeFilterTable("RenderLowpass", {8, 24}),
                         itest::userTableInfo("RenderLowpass"));
    return proc;
}

} // namespace

TEST_CASE_METHOD(JuceEnv,
                 "offline render: sweep + noise finite and bounded in every phase mode (tier A)",
                 "[integration][render]") {
    for (int mode = 0; mode < 4; ++mode) {
        DYNAMIC_SECTION("phaseMode=" << modeName(mode)) {
            for (int sig = 0; sig < 2; ++sig) {
                DYNAMIC_SECTION("signal=" << (sig == 0 ? "sweep100to10k" : "whiteNoise")) {
                    auto proc = makePreparedProcessor(mode, 1.0f);

                    juce::AudioBuffer<float> input(2, kRenderSamples);
                    if (sig == 0)
                        itest::fillExpSweep(input, kFs, 100.0, 10000.0, 0.5f);
                    else
                        itest::fillNoise(input, 0xF17E5u + static_cast<std::uint32_t>(mode));

                    itest::RenderPlan plan;
                    plan.blockSize = kBlock;
                    plan.warmupBlocks = 4;
                    plan.scanRampMidRender = true;       // automation ramp during the render
                    plan.stateSaveLoadMidRender = true;  // state cycle must not corrupt audio

                    const auto res = itest::renderThroughProcessor(*proc, input, plan);

                    REQUIRE(res.blocksRendered > 0);
                    REQUIRE(itest::allFinite(res.output));

                    for (int ch = 0; ch < 2; ++ch) {
                        const float rms = itest::rmsDb(res.output, ch);
                        INFO("channel " << ch << " full-render RMS = " << rms << " dBFS");
                        REQUIRE(rms <= 6.0f);
                        REQUIRE(rms >= -80.0f);
                    }

                    INFO("latency must not change during a render (host + engine instance)");
                    REQUIRE(res.hostLatencyConstant);
                    REQUIRE(res.engineLatencyConstant);
                    REQUIRE(res.hostLatencyAtStart >= 0);
                    REQUIRE(res.engineLatencyAtStart >= 0);
                }
            }
        }
    }
}

TEST_CASE_METHOD(JuceEnv,
                 "real engine: wet differs from dry through a strongly-filtering table (tier B)",
                 "[integration][render][tierB]") {
    if (!itest::tierBActiveOrNotice("wet != dry behavioral check")) {
        SUCCEED();
        return;
    }

    auto proc = makePreparedProcessor(static_cast<int>(ftc::PhaseMode::Minimum), 1.0f);

    juce::AudioBuffer<float> input(2, kRenderSamples / 2);
    itest::fillNoise(input, 0xD1FFu);

    itest::RenderPlan plan;
    plan.blockSize = kBlock;
    plan.warmupBlocks = 4;
    const auto res = itest::renderThroughProcessor(*proc, input, plan);

    REQUIRE(itest::allFinite(res.output));
    // Minimum mode: latency 0, so compare directly. A lowpass at ~1/3 of the band must leave
    // a large residual against the dry input (the removed high band).
    const float diffDb = itest::nullDepthDb(res.output, input, res.engineLatencyAtStart, 8192);
    INFO("out-vs-dry residual = " << diffDb << " dB (passthrough would be -inf)");
    REQUIRE(diffDb > -40.0f);
}

TEST_CASE_METHOD(JuceEnv,
                 "real engine: mix=0 nulls against latency-delayed dry input per mode (tier B)",
                 "[integration][render][tierB]") {
    if (!itest::tierBActiveOrNotice("mix=0 latency-aligned dry null")) {
        SUCCEED();
        return;
    }

    for (int mode = 0; mode < 4; ++mode) {
        DYNAMIC_SECTION("phaseMode=" << modeName(mode)) {
            auto proc = makePreparedProcessor(mode, 0.0f); // fully dry

            juce::AudioBuffer<float> input(2, kRenderSamples / 2);
            itest::fillNoise(input, 0xA11CEu + static_cast<std::uint32_t>(mode));

            itest::RenderPlan plan;
            plan.blockSize = kBlock;
            plan.warmupBlocks = 4;
            const auto res = itest::renderThroughProcessor(*proc, input, plan);

            REQUIRE(itest::allFinite(res.output));
            const int latency = res.engineLatencyAtStart;
            REQUIRE(latency >= 0);

            const int start = std::max(2 * latency, 8192); // skip warmup/ramp-in region
            const float null = itest::nullDepthDb(res.output, input, latency, start);
            INFO("mode " << modeName(mode) << ": null vs input delayed by " << latency
                         << " samples = " << null << " dB");
            REQUIRE(null < -60.0f);
        }
    }
}

TEST_CASE_METHOD(JuceEnv,
                 "real engine: per-mode instance latency matches latencySamplesFor (tier B)",
                 "[integration][render][tierB]") {
    if (!itest::tierBActiveOrNotice("instance latency == latencySamplesFor(mode, fs)")) {
        SUCCEED();
        return;
    }

    for (int mode = 0; mode < 4; ++mode) {
        DYNAMIC_SECTION("phaseMode=" << modeName(mode)) {
            auto proc = makePreparedProcessor(mode, 1.0f);

            juce::AudioBuffer<float> input(2, 4 * kBlock);
            itest::fillNoise(input, 0xBEEFu);
            itest::RenderPlan plan;
            plan.blockSize = kBlock;
            plan.warmupBlocks = 0;
            (void)itest::renderThroughProcessor(*proc, input, plan);

            const int expected = ftc::FilterTableEngine::latencySamplesFor(
                static_cast<ftc::PhaseMode>(mode), kFs);
            INFO("mode " << modeName(mode) << " expects " << expected << " samples");
            REQUIRE(proc->engine().latencySamples() == expected);

            // Host-visible latency follows via the ~30 Hz message-thread poll once pumped.
            itest::pumpMessageLoop(150);
            REQUIRE(proc->getLatencySamples() == expected);
        }
    }
}
