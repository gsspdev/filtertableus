// WavImporter acceptance tests (brief B, acceptance 1-4; + Wave-3.1 non-finite scrub).
#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <limits>

#include "WtTestHelpers.h"
#include "ftc/WavetableData.h"
#include "wavetable/FrameOps.h"
#include "wavetable/WavImporter.h"

using namespace wt_test;
using ftus::ImportResult;
using ftus::WavImporter;

namespace {

constexpr int kFrame = ftus::wtio::kFrameLength;

std::vector<float> makeWavetableSamples(int numFrames, int cyclesFirst, int cyclesMid,
                                        int cyclesLast) {
    std::vector<float> samples(static_cast<size_t>(numFrames) * kFrame);
    for (int f = 0; f < numFrames; ++f) {
        const int cycles = f == 0 ? cyclesFirst : (f == numFrames - 1 ? cyclesLast : cyclesMid);
        for (int n = 0; n < kFrame; ++n)
            samples[static_cast<size_t>(f) * kFrame + static_cast<size_t>(n)] =
                0.8f * std::sin(2.0f * 3.14159265f * static_cast<float>(cycles) *
                                static_cast<float>(n) / static_cast<float>(kFrame));
    }
    return samples;
}

} // namespace

TEST_CASE_METHOD(JuceEnv, "wt: import of a 4x2048 wavetable WAV is deterministic",
                 "[wavetable][importer]") {
    TempDir tmp;

    // Four distinct frames (1..4 cycles of sine).
    std::vector<float> samples(static_cast<size_t>(4) * kFrame);
    for (int f = 0; f < 4; ++f)
        for (int n = 0; n < kFrame; ++n)
            samples[static_cast<size_t>(f) * kFrame + static_cast<size_t>(n)] =
                0.7f * std::sin(2.0f * 3.14159265f * static_cast<float>(f + 1) *
                                static_cast<float>(n) / static_cast<float>(kFrame)) +
                0.2f * std::sin(2.0f * 3.14159265f * 7.0f * static_cast<float>(n) /
                                static_cast<float>(kFrame) + 0.4f);

    const auto file = writeWav(tmp.dir(), "table4.wav", {samples}, 44100.0);

    const ImportResult a = WavImporter::importFile(file);
    const ImportResult b = WavImporter::importFile(file);

    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.numFrames == 4);
    REQUIRE(b.numFrames == 4);
    REQUIRE(a.frames.size() == static_cast<size_t>(4) * kFrame);
    REQUIRE(a.sourceType == ftus::TableSourceInfo::Type::UserFile);
    REQUIRE(allFinite(a.frames));

    // Bit-equal across two imports of the same file (pipeline determinism).
    REQUIRE(a.frames.size() == b.frames.size());
    REQUIRE(std::memcmp(a.frames.data(), b.frames.data(),
                        a.frames.size() * sizeof(float)) == 0);

    // Post-processing applied: every frame peaks at 0.9.
    for (int f = 0; f < a.numFrames; ++f) {
        const std::span<const float> frame{a.frames.data() + static_cast<size_t>(f) * kFrame,
                                           static_cast<size_t>(kFrame)};
        REQUIRE(peakAbs(frame) <= 0.9001f);
        REQUIRE(peakAbs(frame) >= 0.85f);
    }
}

TEST_CASE_METHOD(JuceEnv, "wt: 300-frame wavetable decimates to 256 keeping endpoints",
                 "[wavetable][importer]") {
    TempDir tmp;

    // Marker frames: frame 0 = 1 cycle, frame 299 = 5 cycles, everything else 2 cycles.
    const auto samples = makeWavetableSamples(300, 1, 2, 5);
    const auto file = writeWav(tmp.dir(), "table300.wav", {samples}, 48000.0);

    const ImportResult r = WavImporter::importFile(file);
    REQUIRE(r.ok);
    REQUIRE(r.numFrames == 256);
    REQUIRE(r.frames.size() == static_cast<size_t>(256) * kFrame);

    auto frame = [&](int f) {
        return std::span<const float>{r.frames.data() + static_cast<size_t>(f) * kFrame,
                                      static_cast<size_t>(kFrame)};
    };
    REQUIRE(dominantBin(frame(0), 8) == 1);    // first input frame preserved
    REQUIRE(dominantBin(frame(255), 8) == 5);  // last input frame preserved
    REQUIRE(dominantBin(frame(128), 8) == 2);  // middle is middle
}

TEST_CASE_METHOD(JuceEnv, "wt: 600-sample saw imports as one periodic 2048 frame",
                 "[wavetable][importer]") {
    TempDir tmp;

    // One saw ramp -1 -> +1 across 600 samples: a single-cycle file.
    std::vector<float> ramp(600);
    for (int n = 0; n < 600; ++n)
        ramp[static_cast<size_t>(n)] = -1.0f + 2.0f * static_cast<float>(n) / 599.0f;
    const auto file = writeWav(tmp.dir(), "saw600.wav", {ramp}, 44100.0);

    const ImportResult r = WavImporter::importFile(file);
    REQUIRE(r.ok);
    REQUIRE(r.numFrames == 1);
    REQUIRE(r.frames.size() == static_cast<size_t>(kFrame));
    REQUIRE(allFinite(r.frames));
    REQUIRE(peakAbs(r.frames) <= 0.9001f);
    REQUIRE(peakAbs(r.frames) >= 0.85f);

    // Periodic: wrap discontinuity below 0.05 despite the saw edge.
    REQUIRE(std::abs(r.frames[static_cast<size_t>(kFrame - 1)] - r.frames[0]) < 0.05f);
}

TEST_CASE_METHOD(JuceEnv, "wt: stereo files mix down to 0.5*(L+R)", "[wavetable][importer]") {
    TempDir tmp;

    std::vector<float> left(static_cast<size_t>(kFrame));
    std::vector<float> right(static_cast<size_t>(kFrame));
    for (int n = 0; n < kFrame; ++n) {
        const float s = std::sin(2.0f * 3.14159265f * 3.0f * static_cast<float>(n) /
                                 static_cast<float>(kFrame));
        left[static_cast<size_t>(n)] = 0.9f * s;
        right[static_cast<size_t>(n)] = 0.3f * s;
    }
    const auto stereoFile = writeWav(tmp.dir(), "stereo.wav", {left, right}, 44100.0);

    // Equivalent mono content: 0.5*(L+R) = 0.6*s.
    std::vector<float> mixed(static_cast<size_t>(kFrame));
    for (int n = 0; n < kFrame; ++n)
        mixed[static_cast<size_t>(n)] =
            0.5f * (left[static_cast<size_t>(n)] + right[static_cast<size_t>(n)]);
    const auto monoFile = writeWav(tmp.dir(), "mono.wav", {mixed}, 44100.0);

    const ImportResult s = WavImporter::importFile(stereoFile);
    const ImportResult m = WavImporter::importFile(monoFile);
    REQUIRE(s.ok);
    REQUIRE(m.ok);
    REQUIRE(s.numFrames == 1);
    REQUIRE(s.frames.size() == m.frames.size());
    REQUIRE(std::memcmp(s.frames.data(), m.frames.data(),
                        s.frames.size() * sizeof(float)) == 0);
}

TEST_CASE_METHOD(JuceEnv, "wt: garbage inputs fail cleanly without partial tables",
                 "[wavetable][importer]") {
    TempDir tmp;

    SECTION("truncated WAV") {
        const auto samples = makeWavetableSamples(4, 1, 2, 3);
        const auto good = writeWav(tmp.dir(), "good.wav", {samples}, 44100.0);
        juce::MemoryBlock bytes;
        REQUIRE(good.loadFileAsData(bytes));
        const auto truncated = tmp.dir().getChildFile("truncated.wav");
        REQUIRE(truncated.replaceWithData(bytes.getData(), 40)); // mid-header cut

        const ImportResult r = WavImporter::importFile(truncated);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errorMessage.isNotEmpty());
        REQUIRE(r.numFrames == 0);
        REQUIRE(r.frames.empty());
    }

    SECTION("text file renamed .wav") {
        const auto fake = tmp.dir().getChildFile("fake.wav");
        REQUIRE(fake.replaceWithText("This is definitely not RIFF data. Not even close."));

        const ImportResult r = WavImporter::importFile(fake);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errorMessage.isNotEmpty());
        REQUIRE(r.frames.empty());
    }

    SECTION("zero-byte file") {
        const auto empty = tmp.dir().getChildFile("empty.wav");
        REQUIRE(empty.create().wasOk());

        const ImportResult r = WavImporter::importFile(empty);
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errorMessage.isNotEmpty());
        REQUIRE(r.frames.empty());
    }

    SECTION("nonexistent file") {
        const ImportResult r = WavImporter::importFile(tmp.dir().getChildFile("missing.wav"));
        REQUIRE_FALSE(r.ok);
        REQUIRE(r.errorMessage.isNotEmpty());
    }
}

// Wave-3.1: non-finite samples (float WAVs can legally carry NaN/inf bit patterns) are
// scrubbed to 0 at the START of the import chain, so removeDc/normalizePeak and the
// downstream ftc::WavetableData::analyze FFT never see them — a NaN-bearing file must load
// as a fully finite, analyzable table instead of poisoning the wet path.
TEST_CASE_METHOD(JuceEnv, "wt: non-finite input samples are scrubbed silently at import",
                 "[wavetable][importer][robust]") {
    SECTION("exact-multiple wavetable rung") {
        auto samples = makeWavetableSamples(2, 3, 5, 7);
        samples[100] = std::numeric_limits<float>::quiet_NaN();
        samples[kFrame + 200] = std::numeric_limits<float>::infinity();
        samples[kFrame + 201] = -std::numeric_limits<float>::infinity();

        const ImportResult r = WavImporter::importAudio(std::move(samples), 44100.0);
        REQUIRE(r.ok);
        REQUIRE(r.numFrames == 2);
        REQUIRE(allFinite(r.frames));

        auto table = ftc::WavetableData::analyze(r.frames, r.numFrames, "scrubbed");
        REQUIRE(table != nullptr);
        for (int f = 0; f < table->numFrames(); ++f)
            REQUIRE(allFinite(table->magnitudes(f)));
    }

    SECTION("single-cycle rung") {
        std::vector<float> cycle(600);
        for (size_t n = 0; n < cycle.size(); ++n)
            cycle[n] = 0.7f * std::sin(2.0f * 3.14159265f * static_cast<float>(n)
                                       / static_cast<float>(cycle.size()));
        cycle[33] = std::numeric_limits<float>::quiet_NaN();

        const ImportResult r = WavImporter::importAudio(std::move(cycle), 44100.0);
        REQUIRE(r.ok);
        REQUIRE(r.numFrames == 1);
        REQUIRE(allFinite(r.frames));
        REQUIRE(ftc::WavetableData::analyze(r.frames, 1, "scrubbed-cycle") != nullptr);
    }
}
