// FilterTableUS core — shared plain types. FROZEN after Phase 0.
#pragma once
#include <cstdint>

namespace ftc {

/// Kernel phase-reconstruction mode. Latency: Minimum/Raw = 0, Linear/Original = L/2.
enum class PhaseMode : int { Minimum = 0, Linear, Original, Raw };

enum class LfoShape : int { Sine = 0, Triangle, SawUp, SawDown, Square, SampleHold };

/// Tempo-sync divisions, ordered slow -> fast. D = dotted (1.5x), T = triplet (2/3x).
enum class SyncDivision : int {
    W8 = 0, W4, W2, W1,          // 8/1 4/1 2/1 1/1 (whole-note multiples)
    Half, HalfT,                 // 1/2, 1/2T
    QuarterD, Quarter, QuarterT, // 1/4D, 1/4, 1/4T
    EighthD, Eighth, EighthT,    // 1/8D, 1/8, 1/8T
    SixteenthD, Sixteenth, SixteenthT, // 1/16D, 1/16, 1/16T
    ThirtySecond,                // 1/32
    NumDivisions
};

/// Beats (quarter notes) per LFO cycle for a division.
inline double beatsPerCycle(SyncDivision d) noexcept {
    switch (d) {
        case SyncDivision::W8: return 32.0;  case SyncDivision::W4: return 16.0;
        case SyncDivision::W2: return 8.0;   case SyncDivision::W1: return 4.0;
        case SyncDivision::Half: return 2.0; case SyncDivision::HalfT: return 4.0 / 3.0;
        case SyncDivision::QuarterD: return 1.5; case SyncDivision::Quarter: return 1.0;
        case SyncDivision::QuarterT: return 2.0 / 3.0;
        case SyncDivision::EighthD: return 0.75; case SyncDivision::Eighth: return 0.5;
        case SyncDivision::EighthT: return 1.0 / 3.0;
        case SyncDivision::SixteenthD: return 0.375; case SyncDivision::Sixteenth: return 0.25;
        case SyncDivision::SixteenthT: return 1.0 / 6.0;
        case SyncDivision::ThirtySecond: return 0.125;
        default: return 1.0;
    }
}

/// Host transport snapshot for one process() call. valid=false when the host provides nothing.
struct TransportInfo {
    double bpm = 120.0;
    double ppqPosition = 0.0;
    bool playing = false;
    bool valid = false;
};

/// One MIDI note event within the current block.
struct NoteEvent {
    int sampleOffset = 0;
    std::uint8_t note = 60;
    std::uint8_t velocity = 100;
    bool noteOn = true;
};

} // namespace ftc
