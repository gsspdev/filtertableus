// SampleConverter acceptance tests (brief B, acceptance 5).
#include <catch2/catch_test_macros.hpp>

#include "WtTestHelpers.h"
#include "wavetable/FrameOps.h"
#include "wavetable/SampleConverter.h"
#include "wavetable/WavImporter.h"

using namespace wt_test;
using ftus::SampleConverter;

namespace {
constexpr int kFrame = ftus::wtio::kFrameLength;
constexpr double kFs = 44100.0;

std::vector<float> noisySaw(double freq, int numSamples, uint32_t seed) {
    auto saw = sawWave(freq, kFs, numSamples, 0.8f);
    const auto noise = whiteNoise(numSamples, 0.8f * 0.0316f, seed); // -30 dB vs saw peak
    for (size_t i = 0; i < saw.size(); ++i)
        saw[i] += noise[i];
    return saw;
}
} // namespace

TEST_CASE("wt: converter detects saw f0 within 0.5% at 110/220/440 Hz",
          "[wavetable][converter]") {
    const double freqs[] = {110.0, 220.0, 440.0};
    uint32_t seed = 1;
    for (const double f : freqs) {
        const auto mono = noisySaw(f, static_cast<int>(kFs * 2.0), seed++);
        const auto r = SampleConverter::convert(mono, kFs);

        INFO("f0 = " << f << " Hz, detected = " << r.detectedF0);
        REQUIRE(r.ok);
        REQUIRE(r.numFrames >= 1);
        REQUIRE(r.frames.size() ==
                static_cast<size_t>(r.numFrames) * static_cast<size_t>(kFrame));
        REQUIRE(std::abs(r.detectedF0 - f) / f < 0.005);
        REQUIRE(allFinite(r.frames));

        // Frames normalized to the 0.9 peak target.
        for (int frame = 0; frame < r.numFrames; ++frame) {
            const std::span<const float> data{
                r.frames.data() + static_cast<size_t>(frame) * kFrame,
                static_cast<size_t>(kFrame)};
            REQUIRE(peakAbs(data) <= 0.9001f);
            REQUIRE(peakAbs(data) >= 0.6f);
        }
    }
}

TEST_CASE("wt: converter caps output at 256 evenly strided periods",
          "[wavetable][converter]") {
    // 4 seconds of 440 Hz -> ~1760 periods, must come back as 256 frames.
    const auto mono = noisySaw(440.0, static_cast<int>(kFs * 4.0), 7);
    const auto r = SampleConverter::convert(mono, kFs);
    REQUIRE(r.ok);
    REQUIRE(r.numFrames == 256);
}

TEST_CASE("wt: converter rejects unpitched input with the exact pitch error",
          "[wavetable][converter]") {
    const auto noise = whiteNoise(static_cast<int>(kFs * 2.0), 0.8f, 42);
    const auto r = SampleConverter::convert(noise, kFs);

    REQUIRE_FALSE(r.ok);
    REQUIRE(r.frames.empty());
    REQUIRE(r.numFrames == 0);
    REQUIRE(r.errorMessage == SampleConverter::pitchErrorMessage());
    REQUIRE(r.errorMessage.contains("Couldn't detect a pitch"));
    REQUIRE(SampleConverter::pitchErrorMessage().isNotEmpty());
}

TEST_CASE("wt: converter rejects too-short input cleanly", "[wavetable][converter]") {
    const auto tiny = whiteNoise(100, 0.5f, 3);
    const auto r = SampleConverter::convert(tiny, kFs);
    REQUIRE_FALSE(r.ok);
    REQUIRE(r.errorMessage == SampleConverter::pitchErrorMessage());
}

TEST_CASE_METHOD(JuceEnv, "wt: importer routes long non-divisible files to the converter",
                 "[wavetable][converter][importer]") {
    TempDir tmp;
    // 30000 samples: too long for single-cycle, not divisible by 2048.
    const auto mono = noisySaw(220.0, 30000, 11);
    const auto file = writeWav(tmp.dir(), "sample220.wav", {mono}, kFs);

    const auto r = ftus::WavImporter::importFile(file);
    REQUIRE(r.ok);
    REQUIRE(r.sourceType == ftus::TableSourceInfo::Type::Converted);
    REQUIRE(r.numFrames >= 1);
    REQUIRE(r.detectedF0 > 0.0);
    REQUIRE(std::abs(r.detectedF0 - 220.0) / 220.0 < 0.005);
}

TEST_CASE("wt: converter frames wrap smoothly", "[wavetable][converter]") {
    const auto mono = noisySaw(110.0, static_cast<int>(kFs * 1.5), 23);
    const auto r = SampleConverter::convert(mono, kFs);
    REQUIRE(r.ok);
    for (int frame = 0; frame < r.numFrames; ++frame) {
        const float* data = r.frames.data() + static_cast<size_t>(frame) * kFrame;
        REQUIRE(std::abs(data[kFrame - 1] - data[0]) < 0.15f); // noisy input: relaxed bound
    }
}
