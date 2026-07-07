// Shared helpers for the wavetable I/O test suite (owned by the wavetable workstream).
// Headless-safe: JUCE lifetime comes from the JuceEnv fixture; WAVs go to a per-run temp dir.
#pragma once
#include <cmath>
#include <memory>
#include <random>
#include <span>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

namespace wt_test {

/// Catch2 fixture: initialises JUCE (MessageManager etc.) for the test's lifetime.
struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

/// Fresh per-test temp directory that cleans up after itself.
class TempDir {
public:
    TempDir() {
        dir_ = juce::File::getSpecialLocation(juce::File::tempDirectory)
                   .getChildFile("ftus_wt_tests")
                   .getChildFile(juce::Uuid().toString());
        REQUIRE(dir_.createDirectory().wasOk());
    }
    ~TempDir() { dir_.deleteRecursively(); }
    const juce::File& dir() const { return dir_; }

private:
    juce::File dir_;
};

/// Write a 32-bit float WAV; channels must all share one length.
inline juce::File writeWav(const juce::File& dir, const juce::String& name,
                           const std::vector<std::vector<float>>& channels, double sampleRate) {
    REQUIRE(!channels.empty());
    const int numChannels = static_cast<int>(channels.size());
    const int numSamples = static_cast<int>(channels[0].size());

    juce::File file = dir.getChildFile(name);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    REQUIRE(stream != nullptr);

    juce::WavAudioFormat format;
    auto writer = format.createWriterFor(
        stream, juce::AudioFormatWriterOptions{}
                    .withSampleRate(sampleRate)
                    .withNumChannels(numChannels)
                    .withBitsPerSample(32)
                    .withSampleFormat(juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
    REQUIRE(writer != nullptr);

    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int ch = 0; ch < numChannels; ++ch) {
        REQUIRE(static_cast<int>(channels[static_cast<size_t>(ch)].size()) == numSamples);
        buffer.copyFrom(ch, 0, channels[static_cast<size_t>(ch)].data(), numSamples);
    }
    REQUIRE(writer->writeFromAudioSampleBuffer(buffer, 0, numSamples));
    writer.reset(); // flush + close
    REQUIRE(file.existsAsFile());
    return file;
}

/// Naive sawtooth, phase starts at the rising zero crossing (0 -> +1, wrap to -1).
inline std::vector<float> sawWave(double freq, double sampleRate, int numSamples,
                                  float amplitude = 0.8f) {
    std::vector<float> v(static_cast<size_t>(numSamples));
    double phase = 0.5; // saw value 2*phase-1 == 0, rising
    const double inc = freq / sampleRate;
    for (int i = 0; i < numSamples; ++i) {
        v[static_cast<size_t>(i)] = amplitude * static_cast<float>(2.0 * phase - 1.0);
        phase += inc;
        if (phase >= 1.0)
            phase -= 1.0;
    }
    return v;
}

/// Deterministic uniform white noise in [-amplitude, amplitude].
inline std::vector<float> whiteNoise(int numSamples, float amplitude, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> dist(-amplitude, amplitude);
    std::vector<float> v(static_cast<size_t>(numSamples));
    for (auto& s : v)
        s = dist(rng);
    return v;
}

/// |DFT| of `frame` at integer cycle count `bin` (naive, test-sized).
inline double dftMagnitude(std::span<const float> frame, int bin) {
    const double n = static_cast<double>(frame.size());
    double re = 0.0, im = 0.0;
    for (size_t i = 0; i < frame.size(); ++i) {
        const double w = 2.0 * 3.14159265358979323846 * bin * static_cast<double>(i) / n;
        re += frame[i] * std::cos(w);
        im -= frame[i] * std::sin(w);
    }
    return std::sqrt(re * re + im * im);
}

/// The strongest bin in [1, maxBin] — identifies which marker sine a frame carries.
inline int dominantBin(std::span<const float> frame, int maxBin) {
    int best = 1;
    double bestMag = -1.0;
    for (int k = 1; k <= maxBin; ++k) {
        const double m = dftMagnitude(frame, k);
        if (m > bestMag) {
            bestMag = m;
            best = k;
        }
    }
    return best;
}

inline float peakAbs(std::span<const float> frame) {
    float p = 0.0f;
    for (const float v : frame)
        p = std::max(p, std::abs(v));
    return p;
}

inline bool allFinite(std::span<const float> data) {
    for (const float v : data)
        if (!std::isfinite(v))
            return false;
    return true;
}

} // namespace wt_test
