// FilterTableUS core — host-facing parameter snapshot (trivially copyable POD).
// FROZEN after Phase 0. The plugin shell fills one of these from APVTS atomics at the top
// of every processBlock and hands it to FilterTableEngine::setParameters (RT-safe copy).
// The ENGINE owns all smoothing; values here are raw targets.
#pragma once
#include "ftc/Types.h"

namespace ftc {

struct LfoParams {
    LfoShape shape = LfoShape::Sine;
    bool tempoSync = false;
    float rateHz = 1.0f;                       // 0.02 .. 20, used when !tempoSync
    SyncDivision division = SyncDivision::Quarter;
    bool retrigger = false;                    // reset phase on MIDI note-on
    float toScan = 0.0f;                       // -1..+1; +/-1 = +/- full scan range
    float toCutoff = 0.0f;                     // -1..+1; +/-1 = +/- 48 semitones
};

struct EnvParams {
    float sensitivityDb = 0.0f;                // -24 .. +24 dB input gain into the follower
    float attackMs = 10.0f;                    // 0.1 .. 500
    float releaseMs = 200.0f;                  // 1 .. 2000
    float toScan = 0.0f;                       // -1..+1
    float toCutoff = 0.0f;                     // -1..+1 (+/-48 st)
};

struct Parameters {
    float scan = 0.0f;                         // 0..1 position through the wavetable
    float cutoffHz = 440.0f;                   // 20..20000; harmonic #24 lands here
    float resonance = 0.0f;                    // -1..+1 contrast (emphasize/attenuate peaks+troughs)
    float mix = 1.0f;                          // 0..1 dry/wet (dry is latency-aligned by the engine)
    PhaseMode mode = PhaseMode::Minimum;
    float keytrack = 0.0f;                     // -1..+1; +/-1 = +/-1 semitone per semitone from A4 (MIDI 69)
    float outGainDb = 0.0f;                    // -24 .. +12
    LfoParams lfo1{};
    LfoParams lfo2{LfoShape::Sine, false, 0.25f, SyncDivision::Quarter, false, 0.0f, 0.0f};
    EnvParams env{};
};

} // namespace ftc
