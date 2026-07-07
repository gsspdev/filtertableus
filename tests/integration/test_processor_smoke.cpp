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
