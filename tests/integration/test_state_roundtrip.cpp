// Full processor state round-trip through get/setStateInformation:
//   * every one of the 27 parameters is moved OFF its default, saved, restored into a fresh
//     processor, and must come back BIT-EXACT (tier A — must hold with the Phase 0 APVTS-only
//     StateManagerStub and with the real StateManagerImpl).
//   * wavetable embedding is checked BEHAVIORALLY (tier auto-tightens): if the restored
//     processor exposes a table via engine().currentTableForUi(), its samples must match the
//     saved table bit-exactly (codec is lossless gzip-f32le-v1). Until the state workstream
//     lands table embedding, that branch prints a notice and is skipped.
#include <map>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.h"

using itest::JuceEnv;

TEST_CASE_METHOD(JuceEnv, "full state round-trip: params bit-exact + table when embedded",
                 "[integration][state]") {
    ftus::FtusAudioProcessor source;
    source.setPlayConfigDetails(2, 2, 48000.0, 512);
    source.prepareToPlay(48000.0, 512);

    const auto table = itest::makeFilterTable("RoundTripTable", {4, 12, 40});
    source.adoptWavetable(table, itest::userTableInfo("RoundTripTable"));

    // ---- move EVERY parameter off its default --------------------------------------------
    for (auto* p : source.getParameters()) {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        REQUIRE(rp != nullptr);
        if (auto* b = dynamic_cast<juce::AudioParameterBool*>(rp)) {
            b->setValueNotifyingHost(b->get() ? 0.0f : 1.0f);
        } else if (auto* c = dynamic_cast<juce::AudioParameterChoice*>(rp)) {
            const int n = c->choices.size();
            const int newIndex = (c->getIndex() + 1) % n;
            c->setValueNotifyingHost(rp->convertTo0to1(static_cast<float>(newIndex)));
        } else {
            const float defNorm = rp->getDefaultValue();
            float norm = defNorm + 0.37f;
            norm -= std::floor(norm); // fract: guaranteed != default
            rp->setValueNotifyingHost(norm);
        }
    }

    // Ground truth AFTER snapping: what the APVTS actually stores now.
    std::map<std::string, float> expected;
    for (auto* p : source.getParameters()) {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        expected[rp->paramID.toStdString()] = rp->getValue();
    }
    REQUIRE(expected.size() == static_cast<size_t>(ftus::kNumParameters));

    juce::MemoryBlock blob;
    source.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);

    // ---- restore into a fresh processor --------------------------------------------------
    ftus::FtusAudioProcessor restored;
    restored.setPlayConfigDetails(2, 2, 48000.0, 512);
    restored.prepareToPlay(48000.0, 512);
    restored.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    itest::pumpMessageLoop(100); // let any posted adoption/broadcast work drain

    for (auto* p : restored.getParameters()) {
        auto* rp = dynamic_cast<juce::RangedAudioParameter*>(p);
        REQUIRE(rp != nullptr);
        const auto id = rp->paramID.toStdString();
        INFO("parameter '" << id << "' must restore bit-exactly");
        REQUIRE(expected.count(id) == 1);
        REQUIRE(rp->getValue() == expected.at(id)); // bit-exact, no tolerance
    }

    // ---- wavetable embedding (auto-tightens when source/state StateManagerImpl lands) ----
    const auto restoredTable = restored.engine().currentTableForUi();
    if (restoredTable == nullptr) {
        std::cout << "[stub-tier] state manager does not yet embed wavetables — table-restore "
                     "assertions skipped (auto-tighten when the state workstream lands)"
                  << std::endl;
        WARN("stub tier: wavetable embedding not active; table-restore checks tighten later");
    } else {
        REQUIRE(restoredTable->numFrames() == table->numFrames());
        for (int f = 0; f < table->numFrames(); ++f) {
            const auto a = table->frame(f);
            const auto b = restoredTable->frame(f);
            REQUIRE(a.size() == b.size());
            bool identical = true;
            for (size_t n = 0; n < a.size(); ++n) {
                if (a[n] != b[n]) { // bit-exact: codec is lossless f32
                    identical = false;
                    break;
                }
            }
            INFO("frame " << f << " must round-trip bit-exactly");
            REQUIRE(identical);
        }
        CHECK(restored.currentTableInfo().displayName == juce::String("RoundTripTable"));
    }

    // A restored processor must still render sanely.
    itest::setBoolParam(restored, ftus::ids::bypass, false); // bypass was flipped on above
    juce::AudioBuffer<float> block(2, 512);
    itest::fillNoise(block, 0x57A7Eu);
    juce::MidiBuffer midi;
    restored.processBlock(block, midi);
    REQUIRE(itest::allFinite(block));
}

TEST_CASE_METHOD(JuceEnv, "state round-trip is stable across a second save/load generation",
                 "[integration][state]") {
    ftus::FtusAudioProcessor a;
    itest::setParamReal(a, ftus::ids::cutoff, 3123.5f);
    itest::setParamReal(a, ftus::ids::resonance, -0.62f);
    itest::setParamReal(a, ftus::ids::scan, 0.4142f);
    itest::setChoiceParam(a, ftus::ids::phaseMode, static_cast<int>(ftc::PhaseMode::Linear));
    itest::setBoolParam(a, ftus::ids::lfo1Sync, true);

    juce::MemoryBlock blobA;
    a.getStateInformation(blobA);

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blobA.getData(), static_cast<int>(blobA.getSize()));
    juce::MemoryBlock blobB;
    b.getStateInformation(blobB);

    ftus::FtusAudioProcessor c;
    c.setStateInformation(blobB.getData(), static_cast<int>(blobB.getSize()));

    for (auto* p : a.getParameters()) {
        auto* pa = dynamic_cast<juce::RangedAudioParameter*>(p);
        auto* pc = dynamic_cast<juce::RangedAudioParameter*>(
            c.state().getParameter(pa->paramID));
        REQUIRE(pc != nullptr);
        INFO("parameter '" << pa->paramID << "' must survive two save/load generations");
        REQUIRE(pc->getValue() == pa->getValue());
    }
}
