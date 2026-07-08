// FilterTableUS core — the engine facade the plugin shell talks to. FROZEN after Phase 0.
//
// Phase 0 ships a PASSTHROUGH implementation (core/src/engine_stub.cpp); the engine-assembly
// workstream replaces it with the real implementation (core/src/engine*.cpp) behind this
// unchanged header. See docs/PLAN.md §2 for the full processing contract.
//
// Thread contract:
//   prepare / reset            host callbacks (audio stopped) — allocate everything worst-case
//   setParameters              audio thread, once per block, RT-safe POD copy
//   process                    audio thread only
//   setWavetable               message thread (any non-audio thread); adoption happens inside
//                              process() at the next block boundary via ObjectHandoff
//   collectGarbage             message thread timer (~30 Hz since Wave-3.1; stays cheap)
//   latencySamples             any thread (atomic read); changes only on phase-mode switches
//   readResponseCurve          GUI/message thread (TripleBuffer consumer)
//   currentScan / envValue     any thread (relaxed atomics; post-modulation UI values)
#pragma once
#include <memory>
#include <span>
#include "ftc/Parameters.h"
#include "ftc/ResponseCurve.h"
#include "ftc/Types.h"
#include "ftc/WavetableData.h"

namespace ftc {

class FilterTableEngine {
public:
    struct PrepareSpec {
        double sampleRate = 48000.0;
        int maxBlockSize = 512;
        int numChannels = 2;
    };

    FilterTableEngine();
    ~FilterTableEngine();
    FilterTableEngine(const FilterTableEngine&) = delete;
    FilterTableEngine& operator=(const FilterTableEngine&) = delete;

    void prepare(const PrepareSpec& spec);
    void reset() noexcept;

    void setParameters(const Parameters& params) noexcept;
    void setWavetable(WavetablePtr table);
    void process(float* const* channels, int numChannels, int numSamples,
                 const TransportInfo& transport, std::span<const NoteEvent> notes) noexcept;

    int latencySamples() const noexcept;
    static int latencySamplesFor(PhaseMode mode, double sampleRate) noexcept;

    void collectGarbage();

    // UI taps
    bool readResponseCurve(ResponseCurve& out) noexcept;
    float currentScan() const noexcept;   // post-modulation scan 0..1
    float envValue() const noexcept;      // envelope follower 0..1
    /// Message-thread view of the most recently published table (GUI waterfall). May be null.
    WavetablePtr currentTableForUi() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ftc
