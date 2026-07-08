// Phase 0 seed of the integration suite (the harness workstream expands this):
// headless processor instantiation, silent render sanity, parameter-count lock,
// APVTS state round-trip through get/setStateInformation.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include <juce_events/juce_events.h>

#include "ftus/PluginIDs.h"
#include "plugin/PluginProcessor.h"

namespace {
struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};
} // namespace

TEST_CASE_METHOD(JuceEnv, "processor instantiates, prepares, and processes silence", "[smoke]") {
    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);
    REQUIRE(proc.getLatencySamples() >= 0);

    juce::AudioBuffer<float> buffer(2, 512);
    buffer.clear();
    juce::MidiBuffer midi;
    for (int i = 0; i < 16; ++i)
        proc.processBlock(buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            REQUIRE(std::isfinite(buffer.getSample(ch, i)));
}

// Wave-3.1 host-matrix gap: some hosts (Logic mono channel strips, mono FX slots) run the
// plugin 1-in/1-out. The layout is declared supported — prove it prepares, processes real
// audio finitely, and reports the exact per-mode latency.
TEST_CASE_METHOD(JuceEnv, "mono host layout (1-in/1-out) processes and reports latency",
                 "[smoke][layout]") {
    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(1, 1, 48000.0, 512);
    REQUIRE(proc.getTotalNumInputChannels() == 1);
    REQUIRE(proc.getTotalNumOutputChannels() == 1);

    proc.prepareToPlay(48000.0, 512);
    // Default mode is Minimum -> zero latency.
    REQUIRE(proc.getLatencySamples()
            == ftc::FilterTableEngine::latencySamplesFor(ftc::PhaseMode::Minimum, 48000.0));

    juce::AudioBuffer<float> buffer(1, 512);
    juce::MidiBuffer midi;
    float* d = buffer.getWritePointer(0);
    double rms = 0.0;
    for (int b = 0; b < 32; ++b) {
        for (int i = 0; i < 512; ++i)
            d[i] = 0.4f * std::sin(2.0 * 3.14159265358979 * 220.0 * (b * 512 + i) / 48000.0);
        proc.processBlock(buffer, midi);
        for (int i = 0; i < 512; ++i) {
            REQUIRE(std::isfinite(d[i]));
            if (b >= 16) // past ramps/latency fill: output must be alive
                rms += static_cast<double>(d[i]) * d[i];
        }
    }
    REQUIRE(std::sqrt(rms / (16.0 * 512.0)) > 1e-4);

    // A latency-bearing mode reports L/2 through the same mono layout after re-prepare.
    auto* mode = proc.state().getParameter(ftus::ids::phaseMode);
    REQUIRE(mode != nullptr);
    mode->setValueNotifyingHost(
        mode->convertTo0to1(static_cast<float>(ftc::PhaseMode::Linear)));
    proc.prepareToPlay(48000.0, 512);
    REQUIRE(proc.getLatencySamples()
            == ftc::FilterTableEngine::latencySamplesFor(ftc::PhaseMode::Linear, 48000.0));
    buffer.clear();
    proc.processBlock(buffer, midi);
    for (int i = 0; i < 512; ++i)
        REQUIRE(std::isfinite(buffer.getSample(0, i)));
}

TEST_CASE_METHOD(JuceEnv, "parameter count is locked at 27 (+ housekeeping)", "[smoke][params]") {
    ftus::FtusAudioProcessor proc;
    int visible = 0;
    for (auto* p : proc.getParameters())
        if (dynamic_cast<juce::AudioProcessorParameterWithID*>(p) != nullptr)
            ++visible;
    REQUIRE(visible == ftus::kNumParameters);
    REQUIRE(proc.getBypassParameter() != nullptr);
}

TEST_CASE_METHOD(JuceEnv, "APVTS state round-trips through get/setStateInformation", "[smoke][state]") {
    ftus::FtusAudioProcessor a;
    a.state().getParameter(ftus::ids::cutoff)->setValueNotifyingHost(0.75f);
    a.state().getParameter(ftus::ids::resonance)->setValueNotifyingHost(0.9f);
    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(b.state().getParameter(ftus::ids::cutoff)->getValue() ==
            Catch::Approx(0.75f).margin(1e-6));
    REQUIRE(b.state().getParameter(ftus::ids::resonance)->getValue() ==
            Catch::Approx(0.9f).margin(1e-6));
}
