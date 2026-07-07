#include "wavetable/WavImporter.h"

#include <algorithm>
#include <cstring>
#include <memory>

#include <juce_audio_formats/juce_audio_formats.h>

#include "wavetable/FrameOps.h"
#include "wavetable/SampleConverter.h"

namespace ftus {

namespace {

ImportResult error(const juce::String& message) {
    ImportResult r;
    r.ok = false;
    r.errorMessage = message;
    return r;
}

/// Decode the first two channels and mix down to mono as 0.5*(L+R).
/// (Mono readers duplicate their channel into L and R, so 0.5*(L+R) is exact passthrough;
/// for > 2 channels JUCE maps reader channels 0/1 onto L/R.)
bool decodeMono(juce::AudioFormatReader& reader, std::vector<float>& mono) {
    const juce::int64 total = reader.lengthInSamples;
    mono.assign(static_cast<size_t>(total), 0.0f);

    constexpr int kBlock = 65536;
    juce::AudioBuffer<float> block(2, kBlock);
    juce::int64 pos = 0;
    while (pos < total) {
        const int n = static_cast<int>(std::min<juce::int64>(kBlock, total - pos));
        if (!reader.read(&block, 0, n, pos, true, true))
            return false;
        const float* l = block.getReadPointer(0);
        const float* r = block.getReadPointer(1);
        float* dst = mono.data() + pos;
        for (int i = 0; i < n; ++i)
            dst[i] = 0.5f * (l[i] + r[i]);
        pos += n;
    }
    return true;
}

} // namespace

ImportResult WavImporter::importFile(const juce::File& file) {
    if (!file.existsAsFile())
        return error("File not found: " + file.getFullPathName());

    auto stream = file.createInputStream();
    if (stream == nullptr)
        return error("Couldn't open " + file.getFileName());

    juce::WavAudioFormat wavFormat;
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(stream.release(), /*deleteStreamIfOpeningFails*/ true));
    if (reader == nullptr)
        return error("Not a readable WAV file: " + file.getFileName());

    if (reader->lengthInSamples <= 0)
        return error(file.getFileName() + " contains no audio");
    if (reader->lengthInSamples > kMaxImportSamples)
        return error(file.getFileName() + " is too long to import");
    if (reader->numChannels < 1)
        return error(file.getFileName() + " has no audio channels");

    std::vector<float> mono;
    if (!decodeMono(*reader, mono))
        return error("Couldn't read audio data from " + file.getFileName());

    auto result = importAudio(std::move(mono), reader->sampleRate);
    if (!result.ok && result.errorMessage.isEmpty())
        result.errorMessage = "Couldn't import " + file.getFileName();
    return result;
}

ImportResult WavImporter::importAudio(std::vector<float> mono, double sampleRate) {
    const auto len = static_cast<juce::int64>(mono.size());
    if (len <= 0)
        return error("The file contains no audio");
    if (len > kMaxImportSamples)
        return error("The file is too long to import");

    constexpr int frameLen = wtio::kFrameLength;

    // Rung 3: exact multiple of 2048 (and a sane frame count) -> it IS a wavetable.
    if (len % frameLen == 0 && len / frameLen <= kMaxWavetableFrames) {
        const int inFrames = static_cast<int>(len / frameLen);
        const int outFrames = std::min(inFrames, wtio::kMaxFrames);
        const auto indices = wtio::evenStrideIndices(inFrames, outFrames);

        ImportResult r;
        r.frames.resize(static_cast<size_t>(outFrames) * frameLen);
        for (int f = 0; f < outFrames; ++f) {
            const float* src = mono.data() + static_cast<size_t>(indices[static_cast<size_t>(f)]) * frameLen;
            float* dst = r.frames.data() + static_cast<size_t>(f) * frameLen;
            std::memcpy(dst, src, sizeof(float) * frameLen);
            wtio::postProcessFrame({dst, static_cast<size_t>(frameLen)});
        }
        r.ok = true;
        r.numFrames = outFrames;
        r.sourceType = TableSourceInfo::Type::UserFile;
        return r;
    }

    // Rung 4: short file -> single cycle resampled to one frame.
    if (len >= kSingleCycleMin && len <= kSingleCycleMax) {
        ImportResult r;
        r.frames.resize(static_cast<size_t>(frameLen));
        wtio::resampleCycleCircular(mono, r.frames);
        wtio::postProcessFrame(r.frames);
        r.ok = true;
        r.numFrames = 1;
        r.sourceType = TableSourceInfo::Type::UserFile;
        return r;
    }

    // Rung 5: hand it to the sample converter.
    auto converted = SampleConverter::convert(mono, sampleRate);
    if (!converted.ok)
        return error(converted.errorMessage);

    ImportResult r;
    r.ok = true;
    r.frames = std::move(converted.frames);
    r.numFrames = converted.numFrames;
    r.sourceType = TableSourceInfo::Type::Converted;
    r.detectedF0 = converted.detectedF0;
    return r;
}

} // namespace ftus
