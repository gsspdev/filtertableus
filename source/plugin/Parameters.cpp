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

// --- explicit text conversions (Wave-3 integration; see docs/INTERFACES.md) -----------------
// CLAP's `param-conversions` validator requires text -> value -> text to be a FIXED POINT.
// JUCE's default float formatting is not (full-precision digits drift by an ulp per pass), so
// every float parameter gets a fixed-decimal formatter + a unit-tolerant parser: reformatting
// the parsed value always reproduces the same string (the parsed value lands within half an
// ulp of the decimal, far from the next rounding boundary).

/// Value printed as-is with a fixed decimal count; parser ignores a trailing unit.
Attrs unitAttrs(int decimals, const char* label) {
    return Attrs()
        .withStringFromValueFunction(
            [decimals](float v, int) { return juce::String(v, decimals); })
        .withValueFromStringFunction(
            [](const juce::String& t) { return t.trimStart().getFloatValue(); })
        .withLabel(label);
}

/// 0..1 (or -1..+1) value displayed as a percentage with one decimal.
Attrs percentAttrs() {
    return Attrs()
        .withStringFromValueFunction(
            [](float v, int) { return juce::String(v * 100.0f, 1); })
        .withValueFromStringFunction(
            [](const juce::String& t) { return t.trimStart().getFloatValue() * 0.01f; })
        .withLabel("%");
}

void addLfo(juce::AudioProcessorValueTreeState::ParameterLayout& layout, int index,
            const char* rateId, const char* syncId, const char* divId, const char* shapeId,
            const char* retrigId, const char* toScanId, const char* toCutoffId,
            float defaultRateHz) {
    const juce::String prefix = "LFO " + juce::String(index + 1) + " ";
    layout.add(std::make_unique<FloatParam>(pid(rateId), prefix + "Rate",
                                            skewedRange(0.02f, 20.0f, 1.0f), defaultRateHz,
                                            unitAttrs(3, "Hz")));
    layout.add(std::make_unique<BoolParam>(pid(syncId), prefix + "Sync", false));
    layout.add(std::make_unique<ChoiceParam>(pid(divId), prefix + "Division",
                                             syncDivisionChoices(), 7 /* 1/4 */));
    layout.add(std::make_unique<ChoiceParam>(pid(shapeId), prefix + "Shape",
                                             lfoShapeChoices(), 0 /* Sine */));
    layout.add(std::make_unique<BoolParam>(pid(retrigId), prefix + "Retrig", false));
    layout.add(std::make_unique<FloatParam>(pid(toScanId), prefix + "> Scan",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<FloatParam>(pid(toCutoffId), prefix + "> Cutoff",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));
}

} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout() {
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<FloatParam>(pid(ids::scan), "Scan",
                                            juce::NormalisableRange<float>(0.0f, 1.0f), 0.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<FloatParam>(pid(ids::cutoff), "Cutoff",
                                            skewedRange(20.0f, 20000.0f, 1000.0f), 440.0f,
                                            unitAttrs(1, "Hz")));
    layout.add(std::make_unique<FloatParam>(pid(ids::resonance), "Resonance",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<FloatParam>(pid(ids::mix), "Mix",
                                            juce::NormalisableRange<float>(0.0f, 1.0f), 1.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<ChoiceParam>(pid(ids::phaseMode), "Phase Mode",
                                             phaseModeChoices(), 0 /* Minimum */));
    layout.add(std::make_unique<FloatParam>(pid(ids::keytrack), "Key Track",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<FloatParam>(pid(ids::outGain), "Output",
                                            juce::NormalisableRange<float>(-24.0f, 12.0f), 0.0f,
                                            unitAttrs(1, "dB")));
    layout.add(std::make_unique<BoolParam>(pid(ids::bypass), "Bypass", false));

    addLfo(layout, 0, ids::lfo1Rate, ids::lfo1Sync, ids::lfo1Div, ids::lfo1Shape,
           ids::lfo1Retrig, ids::lfo1ToScan, ids::lfo1ToCutoff, 1.0f);
    addLfo(layout, 1, ids::lfo2Rate, ids::lfo2Sync, ids::lfo2Div, ids::lfo2Shape,
           ids::lfo2Retrig, ids::lfo2ToScan, ids::lfo2ToCutoff, 0.25f);

    layout.add(std::make_unique<FloatParam>(pid(ids::envSens), "Env Sensitivity",
                                            juce::NormalisableRange<float>(-24.0f, 24.0f), 0.0f,
                                            unitAttrs(1, "dB")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envAttack), "Env Attack",
                                            skewedRange(0.1f, 500.0f, 10.0f), 10.0f,
                                            unitAttrs(2, "ms")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envRelease), "Env Release",
                                            skewedRange(1.0f, 2000.0f, 200.0f), 200.0f,
                                            unitAttrs(1, "ms")));
    layout.add(std::make_unique<FloatParam>(pid(ids::envToScan), "Env > Scan",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));
    layout.add(std::make_unique<FloatParam>(pid(ids::envToCutoff), "Env > Cutoff",
                                            juce::NormalisableRange<float>(-1.0f, 1.0f), 0.0f,
                                            percentAttrs()));

    return layout;
}

} // namespace ftus
