// FilterTableUS shell — single source of truth for host parameters. FROZEN after Phase 0.
// IDs are permanent: never rename, re-range, or reuse. Adding a parameter later = new ID with a
// HIGHER version hint + a migration shim in setStateInformation. The golden-list integration
// test locks this table.
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

namespace ftus {

inline constexpr int kParamVersionHint = 1;
inline constexpr int kNumParameters = 27;

namespace ids {
inline constexpr const char* scan = "scan";               // float 0..1, default 0
inline constexpr const char* cutoff = "cutoff";           // float 20..20000 Hz, skew@1k, default 440
inline constexpr const char* resonance = "resonance";     // float -1..+1, default 0
inline constexpr const char* mix = "mix";                 // float 0..1, default 1
inline constexpr const char* phaseMode = "phaseMode";     // choice Minimum/Linear/Original/Raw
inline constexpr const char* keytrack = "keytrack";       // float -1..+1, default 0
inline constexpr const char* outGain = "outGain";         // float -24..+12 dB, default 0
inline constexpr const char* bypass = "bypass";           // bool, hosted bypass

inline constexpr const char* lfo1Rate = "lfo1Rate";       // float 0.02..20 Hz, skew@1, default 1
inline constexpr const char* lfo1Sync = "lfo1Sync";       // bool, default off
inline constexpr const char* lfo1Div = "lfo1Div";         // choice (16 divisions), default 1/4
inline constexpr const char* lfo1Shape = "lfo1Shape";     // choice (6 shapes), default Sine
inline constexpr const char* lfo1Retrig = "lfo1Retrig";   // bool, default off
inline constexpr const char* lfo1ToScan = "lfo1ToScan";   // float -1..+1, default 0
inline constexpr const char* lfo1ToCutoff = "lfo1ToCutoff"; // float -1..+1 (+/-48 st), default 0

inline constexpr const char* lfo2Rate = "lfo2Rate";       // default 0.25 Hz, else same as lfo1
inline constexpr const char* lfo2Sync = "lfo2Sync";
inline constexpr const char* lfo2Div = "lfo2Div";
inline constexpr const char* lfo2Shape = "lfo2Shape";
inline constexpr const char* lfo2Retrig = "lfo2Retrig";
inline constexpr const char* lfo2ToScan = "lfo2ToScan";
inline constexpr const char* lfo2ToCutoff = "lfo2ToCutoff";

inline constexpr const char* envSens = "envSens";         // float -24..+24 dB, default 0
inline constexpr const char* envAttack = "envAttack";     // float 0.1..500 ms, skew@10, default 10
inline constexpr const char* envRelease = "envRelease";   // float 1..2000 ms, skew@200, default 200
inline constexpr const char* envToScan = "envToScan";     // float -1..+1, default 0
inline constexpr const char* envToCutoff = "envToCutoff"; // float -1..+1 (+/-48 st), default 0
} // namespace ids

/// Choice-string tables. Order MUST match the ftc enums (PhaseMode, LfoShape, SyncDivision).
inline const juce::StringArray& phaseModeChoices() {
    static const juce::StringArray choices{"Minimum", "Linear", "Original", "Raw"};
    return choices;
}
inline const juce::StringArray& lfoShapeChoices() {
    static const juce::StringArray choices{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H"};
    return choices;
}
inline const juce::StringArray& syncDivisionChoices() {
    static const juce::StringArray choices{"8/1", "4/1", "2/1", "1/1",
                                           "1/2", "1/2T",
                                           "1/4D", "1/4", "1/4T",
                                           "1/8D", "1/8", "1/8T",
                                           "1/16D", "1/16", "1/16T",
                                           "1/32"};
    return choices;
}

/// Defined in source/plugin/Parameters.cpp.
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

} // namespace ftus
