// FilterTableUS wavetable I/O — WAV import decision ladder (plan §4).
// Owned by the wavetable workstream.
//
// Ladder, in order:
//   1. not a readable WAV                          -> error
//   2. (Serum "clm " frame-size hint: v1.1 stretch — not implemented, see report)
//   3. length divisible by 2048 (and <= 4096 frames) -> wavetable; > 256 frames decimated
//      by even stride keeping both endpoint frames
//   4. short file, 256..8192 samples               -> single cycle, circular Catmull-Rom
//      resample to one 2048-sample frame
//   5. anything else                               -> SampleConverter (YIN pitch + slicing)
//
// Multi-channel input is mixed to mono as 0.5*(L+R) (mono files pass through unchanged).
// Every produced frame is DC-removed, peak-normalized to 0.9 and wrap-crossfaded
// (last 16 samples into the first 16). Deterministic: same input -> identical output.
//
// Threading: allocates and reads files; loader/message thread only (NOT the audio thread).
#pragma once
#include <vector>

#include <juce_core/juce_core.h>

#include "ftus/WavetableCodec.h" // TableSourceInfo

namespace ftus {

struct ImportResult {
    bool ok = false;
    juce::String errorMessage;
    std::vector<float> frames; // numFrames * 2048, frame-major (empty on error)
    int numFrames = 0;
    TableSourceInfo::Type sourceType = TableSourceInfo::Type::UserFile; // UserFile | Converted
    double detectedF0 = 0.0;   // > 0 only when the converter path ran
};

class WavImporter {
public:
    /// Decode `file` as WAV and run the import ladder.
    static ImportResult importFile(const juce::File& file);

    /// The ladder on already-decoded mono audio (the testable seam; importFile calls this).
    static ImportResult importAudio(std::vector<float> mono, double sampleRate);

    static constexpr juce::int64 kMaxImportSamples = 1 << 24; // ~5.8 min @ 48k, 64 MB mono
    static constexpr int kMaxWavetableFrames = 4096;          // divisible-length ceiling
    static constexpr int kSingleCycleMin = 256;
    static constexpr int kSingleCycleMax = 8192;
};

} // namespace ftus
