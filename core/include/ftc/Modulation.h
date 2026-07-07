// FilterTableUS core — internal modulation (2 LFOs, envelope follower, note tracker).
// FROZEN after Phase 0. Implemented by the modulation workstream (core/src/mod*.cpp).
//
// Contract (see docs/PLAN.md §2 "Modulation"):
//  - LFOs: 6 shapes, bipolar -1..+1; free Hz or tempo-synced (timeline-locked to ppq while the
//    host plays, free-running from current BPM when stopped); optional note retrigger; S&H is
//    deterministically seeded and reseeds on transport start.
//  - Envelope follower: rectified mono input, one-pole attack/release, sensitivity dB, out 0..1.
//  - Note tracker: last-note priority, holds the last note after all-notes-off.
//  - evaluate() sums to ModValues:
//      scanOffset  = lfo1*lfo1.toScan + lfo2*lfo2.toScan + env*env.toScan          (engine clamps)
//      cutoffSemis = 48*(lfo1*lfo1.toCutoff + lfo2*lfo2.toCutoff + env*env.toCutoff)
//                    + keytrack*(lastNote - 69)
//
// Threading: prepare() allocates; setParams/beginBlock/evaluate/envValue are audio-thread-only,
// RT-safe. beginBlock() is called once per process() with the block's transport, notes, and a
// mono input pointer (n samples); evaluate(offset) is called at control ticks within the block.
#pragma once
#include <memory>
#include <span>
#include "ftc/Parameters.h"
#include "ftc/Types.h"

namespace ftc {

struct ModValues {
    float scanOffset = 0.0f;   // added to Parameters::scan, then clamped 0..1
    float cutoffSemis = 0.0f;  // added in semitone domain to Parameters::cutoffHz
};

class ModulationEngine {
public:
    ModulationEngine();
    ~ModulationEngine();

    void prepare(double sampleRate, int controlInterval);   // allocates; not RT-safe
    void reset() noexcept;

    void setParams(const Parameters& params) noexcept;      // POD copy of the mod-relevant fields
    void beginBlock(const TransportInfo& transport, std::span<const NoteEvent> notes,
                    const float* monoInput, int numSamples) noexcept;
    ModValues evaluate(int subBlockOffset) noexcept;         // per control tick
    float envValue() const noexcept;                         // 0..1 (UI meter)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ftc
