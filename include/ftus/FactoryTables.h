// FilterTableUS shell — procedural factory wavetables. FROZEN after Phase 0.
// generateFactoryTable is implemented by the wavetable workstream
// (source/wavetable/FactoryGenerators.cpp): pure, deterministic, harmonic-partial-built.
#pragma once
#include <string>
#include <vector>

#include <juce_core/juce_core.h>

namespace ftus {

enum class FactoryTableId : int {
    AnalogMorph = 0, Pwm, VowelMorph, CombSweep, NotchArray, HarmonicLadder,
    OddEvenMorph, SpectralDrift, FormantPeaks, DigitalSteps, MetalCluster, SubBloom,
    NumTables
};

inline constexpr int kNumFactoryTables = static_cast<int>(FactoryTableId::NumTables);

inline const char* factoryTableIdString(FactoryTableId id) {
    switch (id) {
        case FactoryTableId::AnalogMorph: return "analogMorph";
        case FactoryTableId::Pwm: return "pwm";
        case FactoryTableId::VowelMorph: return "vowelMorph";
        case FactoryTableId::CombSweep: return "combSweep";
        case FactoryTableId::NotchArray: return "notchArray";
        case FactoryTableId::HarmonicLadder: return "harmonicLadder";
        case FactoryTableId::OddEvenMorph: return "oddEvenMorph";
        case FactoryTableId::SpectralDrift: return "spectralDrift";
        case FactoryTableId::FormantPeaks: return "formantPeaks";
        case FactoryTableId::DigitalSteps: return "digitalSteps";
        case FactoryTableId::MetalCluster: return "metalCluster";
        case FactoryTableId::SubBloom: return "subBloom";
        default: return "analogMorph";
    }
}

inline const char* factoryTableDisplayName(FactoryTableId id) {
    switch (id) {
        case FactoryTableId::AnalogMorph: return "Analog Morph";
        case FactoryTableId::Pwm: return "PWM";
        case FactoryTableId::VowelMorph: return "Vowel Morph";
        case FactoryTableId::CombSweep: return "Comb Sweep";
        case FactoryTableId::NotchArray: return "Notch Array";
        case FactoryTableId::HarmonicLadder: return "Harmonic Ladder";
        case FactoryTableId::OddEvenMorph: return "Odd/Even Morph";
        case FactoryTableId::SpectralDrift: return "Spectral Drift";
        case FactoryTableId::FormantPeaks: return "Formant Peaks";
        case FactoryTableId::DigitalSteps: return "Digital Steps";
        case FactoryTableId::MetalCluster: return "Metal Cluster";
        case FactoryTableId::SubBloom: return "Sub Bloom";
        default: return "Analog Morph";
    }
}

/// Reverse lookup for state restore; returns nullopt-like sentinel (NumTables) on unknown ids.
inline FactoryTableId factoryTableIdFromString(const juce::String& s) {
    for (int i = 0; i < kNumFactoryTables; ++i)
        if (s == factoryTableIdString(static_cast<FactoryTableId>(i)))
            return static_cast<FactoryTableId>(i);
    return FactoryTableId::NumTables;
}

struct RawTable {
    int numFrames = 0;
    std::vector<float> samples; // numFrames * 2048, frame-major
    std::string name;
};

/// Pure + deterministic. Implemented by the wavetable workstream; NOT callable until it lands
/// (Phase 0 ships no definition — nothing in the scaffold references it).
RawTable generateFactoryTable(FactoryTableId id);

} // namespace ftus
