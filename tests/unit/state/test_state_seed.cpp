// Phase 0 seed — the state workstream replaces/extends this suite.
// The codec is fully implemented in Phase 0, so its round-trip is locked here already.
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "ftc/WavetableData.h"
#include "ftus/WavetableCodec.h"

TEST_CASE("wavetable codec round-trips bit-exactly", "[state][codec]") {
    constexpr int frames = 3;
    std::vector<float> samples(frames * ftc::WavetableData::kFrameLength);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = std::sin(0.001f * static_cast<float>(i)) * 0.9f;

    auto table = ftc::WavetableData::analyze(samples, frames, "codec-test");
    REQUIRE(table != nullptr);

    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::UserFile;
    info.path = "/tmp/some.wav";
    info.displayName = "codec-test";

    const auto tree = ftus::encodeWavetable(*table, info);
    const auto decoded = ftus::decodeWavetable(tree);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->table != nullptr);
    REQUIRE(decoded->table->numFrames() == frames);
    REQUIRE(decoded->info.type == ftus::TableSourceInfo::Type::UserFile);
    REQUIRE(decoded->info.displayName == "codec-test");

    for (int f = 0; f < frames; ++f) {
        const auto original = table->frame(f);
        const auto roundTripped = decoded->table->frame(f);
        for (size_t i = 0; i < original.size(); ++i)
            REQUIRE(original[i] == roundTripped[i]); // bit-exact
    }
}

TEST_CASE("factory-type codec carries no payload and no table", "[state][codec]") {
    std::vector<float> samples(ftc::WavetableData::kFrameLength, 0.5f);
    auto table = ftc::WavetableData::analyze(samples, 1, "f");
    REQUIRE(table != nullptr);

    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::Factory;
    info.factoryId = "analogMorph";
    info.displayName = "Analog Morph";

    const auto tree = ftus::encodeWavetable(*table, info);
    REQUIRE_FALSE(tree.hasProperty("data"));
    const auto decoded = ftus::decodeWavetable(tree);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->table == nullptr);
    REQUIRE(decoded->info.factoryId == "analogMorph");
}

TEST_CASE("codec rejects garbage", "[state][codec]") {
    REQUIRE_FALSE(ftus::decodeWavetable(juce::ValueTree("WRONG")).has_value());
    juce::ValueTree bad("WAVETABLE");
    bad.setProperty("type", "user", nullptr);
    bad.setProperty("encoding", "gzip-f32le-v1", nullptr);
    bad.setProperty("frames", 2, nullptr);
    bad.setProperty("data", "!!!not-base64!!!", nullptr);
    REQUIRE_FALSE(ftus::decodeWavetable(bad).has_value());
}
