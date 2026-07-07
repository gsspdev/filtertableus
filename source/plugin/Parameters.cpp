// APVTS parameter layout — generated strictly from ftus/PluginIDs.h. FROZEN after Phase 0.
#include "ftus/PluginIDs.h"

namespace ftus {

namespace {

using FloatParam = juce::AudioParameterFloat;
using ChoiceParam = juce::AudioParameterChoice;
using BoolParam = juce::AudioParameterBool;
using Attrs = juce::AudioParameterFloatAttributes;

juce::ParameterID pid(const char* id) { return {id, kParamVersionHint}; }

juce::NormalisableRange<float> skewedRange(float min, float max, float centre) {
    juce::NormalisableRange<float> range(min, max);
    range.setSkewForCentre(centre);
    return range;
}

void addLfo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int index,
            const char* rateId, const char* syncId, const char* divId, const char* shapeId,
            const char* retrigId, const char* toScanId, const char* toCutoffId,
            float defaultRateHz) {
    const juce::String prefix = "LFO " + juce::String(index + 1) + " ";
    layout.add(std::make_unique<FloatParam>(pid(rateId), prefix + "Rate",
                                            skewedRange(0.02f, 20.0f, 1.0f), defaultRateHz,
                                            Attrs().withLabel("Hz")));
    layout.add(std::make_unique<BoolParam>(pid(syncId), prefix + "Sync", false));
    layout.add(std::make_unique<ChoiceParam>(pid(divId), prefix + "Division",
                                             syncDivisionChoices(), 7 /* 1/4 */));
    layout.add(std::make_unique<ChoiceParam>(pid(shapeId), prefix + "Shape",
                                             lfoShapeChoices(), 0 /* Sine */));
    layout.add(std::make_unique<BoolParam>(pid(retrigId), prefix + "Retrig", false));
    layout.add(std::make_unique<FloatParam>(pid(toScanId), prefix + "> Scan",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<FloatParam>(pid(toCutoffId), prefix + "> Cutoff",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<FloatParam>(pid(ids::scan), "Scan",
                                            juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<FloatParam>(pid(ids::cutoff), "Cutoff",
                                            skewedRange(20.0f, 20000.0f, 1000.0f), 440.0f,
                                            Attrs().withLabel("Hz")));
    layout.add(std::make_unique<FloatParam>(pid(ids::resonance), "Resonance",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<FloatParam>(pid(ids::mix), "Mix",
                                            juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f));
    layout.add(std::make_unique<ChoiceParam>(pid(ids::phaseMode), "Phase Mode",
                                             phaseModeChoices(), 0 /* Minimum */));
    layout.add(std::make_unique<FloatParam>(pid(ids::keytrack), "Key Track",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<FloatParam>(pid(ids::outGain), "Output",
                                            juce::NormalisableRange<float>(-24.0f, 12.0f), 0.0f,
                                            Attrs().withLabel("dB")));
    layout.add(std::make_unique<BoolParam>(pid(ids::bypass), "Bypass", false));

    addLfo(layout, 0, ids::lfo1Rate, ids::lfo1Sync, ids::lfo1Div, ids::lfo1Shape,
           ids::lfo1Retrig, ids::lfo1ToScan, ids::lfo1ToCutoff, 1.0f);
    addLfo(layout, 1, ids::lfo2Rate, ids::lfo2Sync, ids::lfo2Div, ids::lfo2Shape,
           ids::lfo2Retrig, ids::lfo2ToScan, ids::lfo2ToCutoff, 0.25f);

    layout.add(std::make_unique<FloatParam>(pid(ids::envSens), "Env Sensitivity",
                                            juce::NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
                                            Attrs().withLabel("dB")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envAttack), "Env Attack",
                                            skewedRange(0.1f, 500.0f, 10.0f), 10.0f,
                                            Attrs().withLabel("ms")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envRelease), "Env Release",
                                            skewedRange(1.0f, 2000.0f, 200.0f), 200.0f,
                                            Attrs().withLabel("ms")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envToScan), "Env > Scan",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));
    layout.add(std::make_unique<FloatParam>(pid(ids::envToCutoff), "Env > Cutoff",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f));

    return layout;
}

} // namespace ftus
