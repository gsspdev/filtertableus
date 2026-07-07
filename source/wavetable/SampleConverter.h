// FilterTableUS wavetable I/O — sample -> wavetable converter (plan §4).
// Owned by the wavetable workstream.
//
// Pipeline: YIN pitch detection (frame 4096, hop 1024, 30–2000 Hz, threshold 0.15,
// parabolic interpolation, median filter width 5) -> global median f0 -> slice consecutive
// periods starting at the strongest rising zero-crossing (searched within one period of the
// first usable crossing, so noise can't anchor the slice at the end of the file) ->
// Catmull-Rom resample each period to 2048 -> <=256 periods keeps all, else 256 evenly
// strided -> per-frame DC-remove / peak-normalize 0.9 / wrap-crossfade.
//
// Voiced-frame ratio < 40% or no stable median -> ok=false with pitchErrorMessage().
//
// Threading: pure, allocates; loader/message thread only (NOT the audio thread).
#pragma once
#include <span>
#include <vector>

#include <juce_core/juce_core.h>

namespace ftus {

struct ConvertResult {
    bool ok = false;
    juce::String errorMessage;
    std::vector<float> frames; // numFrames * 2048, frame-major (empty on error)
    int numFrames = 0;
    double detectedF0 = 0.0;   // Hz, global median (0 on error)
};

class SampleConverter {
public:
    static ConvertResult convert(std::span<const float> mono, double sampleRate);

    /// The exact user-facing error for unpitched input.
    static juce::String pitchErrorMessage();

    // Tuning constants (plan §4) — exposed for tests.
    static constexpr int kYinFrame = 4096;
    static constexpr int kYinHop = 1024;
    static constexpr int kYinWindow = 2048; // correlation window inside the 4096 frame
    static constexpr double kMinF0 = 30.0;
    static constexpr double kMaxF0 = 2000.0;
    static constexpr float kYinThreshold = 0.15f;
    static constexpr int kMedianWidth = 5;
    static constexpr float kMinVoicedRatio = 0.4f;
    static constexpr int kMaxAnalysisFrames = 240; // long files: evenly strided YIN frames
};

} // namespace ftus
