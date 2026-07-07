// StateManagerImpl session-state tests: full round-trip with an embedded user table,
// factory-table regeneration, GUI scale, legacy binary fallback, malformed input safety.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

#include <juce_events/juce_events.h>

#include "ftc/WavetableData.h"
#include "ftus/FactoryTables.h"
#include "ftus/PluginIDs.h"
#include "plugin/PluginProcessor.h"

using Catch::Approx;

namespace {

struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

/// Deterministic, bit-exact reproducible frames (integer formula, no libm).
std::vector<float> makeExactFrames(int numFrames) {
    std::vector<float> s(static_cast<size_t>(numFrames) * ftc::WavetableData::kFrameLength);
    for (int f = 0; f < numFrames; ++f)
        for (int i = 0; i < ftc::WavetableData::kFrameLength; ++i)
            s[static_cast<size_t>(f) * ftc::WavetableData::kFrameLength +
              static_cast<size_t>(i)] =
                static_cast<float>((i * 7 + f * 13) % 512) / 256.0f - 1.0f;
    return s;
}

void setDenormalized(ftus::FtusAudioProcessor& p, const char* id, float value) {
    auto* param = p.state().getParameter(id);
    REQUIRE(param != nullptr);
    param->setValueNotifyingHost(param->convertTo0to1(value));
}

float rawValue(ftus::FtusAudioProcessor& p, const char* id) {
    return p.state().getRawParameterValue(id)->load();
}

} // namespace

TEST_CASE_METHOD(JuceEnv, "session state round-trips params and an embedded user table",
                 "[state][session]") {
    ftus::FtusAudioProcessor a;
    setDenormalized(a, ftus::ids::scan, 0.42f);
    setDenormalized(a, ftus::ids::cutoff, 1234.5f);
    setDenormalized(a, ftus::ids::resonance, -0.25f);
    setDenormalized(a, ftus::ids::lfo1Shape, 4.0f); // Square
    setDenormalized(a, ftus::ids::envAttack, 25.0f);

    const int frames = 3;
    const auto samples = makeExactFrames(frames);
    auto table = ftc::WavetableData::analyze(samples, frames, "rt-test");
    REQUIRE(table != nullptr);
    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::UserFile;
    info.path = "/tmp/rt-test.wav";
    info.displayName = "rt-test";
    a.adoptWavetable(table, info);

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    REQUIRE(blob.getSize() > 0);
    const auto text = juce::String::fromUTF8(static_cast<const char*>(blob.getData()),
                                             static_cast<int>(blob.getSize()));
    REQUIRE(text.contains("<FilterTableUS"));
    REQUIRE(text.contains("stateVersion=\"1\""));
    REQUIRE(text.contains("<WAVETABLE"));

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));

    REQUIRE(rawValue(b, ftus::ids::scan) == Approx(0.42f).margin(1e-4));
    REQUIRE(rawValue(b, ftus::ids::cutoff) == Approx(1234.5f).epsilon(1e-3));
    REQUIRE(rawValue(b, ftus::ids::resonance) == Approx(-0.25f).margin(1e-4));
    REQUIRE(static_cast<int>(rawValue(b, ftus::ids::lfo1Shape)) == 4);
    REQUIRE(rawValue(b, ftus::ids::envAttack) == Approx(25.0f).epsilon(1e-3));

    REQUIRE(b.currentTableInfo().type == ftus::TableSourceInfo::Type::UserFile);
    REQUIRE(b.currentTableInfo().displayName == "rt-test");
    const auto decoded = b.engine().currentTableForUi();
    REQUIRE(decoded != nullptr);
    REQUIRE(decoded->numFrames() == frames);
    for (int f = 0; f < frames; ++f) {
        const auto original = table->frame(f);
        const auto restored = decoded->frame(f);
        for (size_t i = 0; i < original.size(); ++i)
            REQUIRE(original[i] == restored[i]); // bit-exact
    }

    // A was edited since its (never-happened) preset load, so the saved session was dirty.
    REQUIRE(b.stateManager().isDirty());
}

TEST_CASE_METHOD(JuceEnv, "factory-type state stores no payload and regenerates identically",
                 "[state][session][factory]") {
    ftus::FtusAudioProcessor a;
    const auto raw = ftus::generateFactoryTable(ftus::FactoryTableId::AnalogMorph);
    REQUIRE(raw.numFrames >= 1);
    REQUIRE(raw.samples.size() ==
            static_cast<size_t>(raw.numFrames) * ftc::WavetableData::kFrameLength);
    auto table = ftc::WavetableData::analyze(raw.samples, raw.numFrames, raw.name);
    REQUIRE(table != nullptr);
    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::Factory;
    info.factoryId = "analogMorph";
    info.displayName = "Analog Morph";
    a.adoptWavetable(table, info);

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    const auto text = juce::String::fromUTF8(static_cast<const char*>(blob.getData()),
                                             static_cast<int>(blob.getSize()));
    const auto xml = juce::parseXML(text);
    REQUIRE(xml != nullptr);
    auto* wt = xml->getChildByName("WAVETABLE");
    REQUIRE(wt != nullptr);
    REQUIRE(wt->getStringAttribute("type") == "factory");
    REQUIRE(!wt->hasAttribute("data")); // no payload for factory tables

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(b.currentTableInfo().type == ftus::TableSourceInfo::Type::Factory);
    REQUIRE(b.currentTableInfo().factoryId == "analogMorph");
    const auto regen = b.engine().currentTableForUi();
    REQUIRE(regen != nullptr);
    REQUIRE(regen->numFrames() == raw.numFrames);
    for (int f = 0; f < raw.numFrames; ++f) {
        const auto original = table->frame(f);
        const auto restored = regen->frame(f);
        for (size_t i = 0; i < original.size(); ++i)
            REQUIRE(original[i] == restored[i]); // deterministic regeneration
    }
}

TEST_CASE_METHOD(JuceEnv, "GUI scale rides the session state", "[state][session][gui]") {
    ftus::FtusAudioProcessor a;
    a.state().state.setProperty("guiScale", 1.5, nullptr);
    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    const auto text = juce::String::fromUTF8(static_cast<const char*>(blob.getData()),
                                             static_cast<int>(blob.getSize()));
    const auto xml = juce::parseXML(text);
    REQUIRE(xml != nullptr);
    auto* gui = xml->getChildByName("GUI");
    REQUIRE(gui != nullptr);
    REQUIRE(gui->getDoubleAttribute("scale") == Approx(1.5));

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(static_cast<double>(b.state().state.getProperty("guiScale", 0.0)) == Approx(1.5));
}

TEST_CASE_METHOD(JuceEnv, "preset name and dirty flag restore from the session",
                 "[state][session]") {
    ftus::FtusAudioProcessor a;
    juce::MemoryBlock cleanBlob;
    a.getStateInformation(cleanBlob); // untouched processor: not dirty, name "Init"

    ftus::FtusAudioProcessor b;
    b.setStateInformation(cleanBlob.getData(), static_cast<int>(cleanBlob.getSize()));
    REQUIRE(b.stateManager().currentPresetName() == "Init");
    REQUIRE(!b.stateManager().isDirty());

    setDenormalized(a, ftus::ids::mix, 0.5f); // edit -> dirty session
    juce::MemoryBlock dirtyBlob;
    a.getStateInformation(dirtyBlob);
    ftus::FtusAudioProcessor c;
    c.setStateInformation(dirtyBlob.getData(), static_cast<int>(dirtyBlob.getSize()));
    REQUIRE(c.stateManager().isDirty());
}

TEST_CASE_METHOD(JuceEnv, "legacy Phase 0 binary session blobs still load",
                 "[state][session][compat]") {
    ftus::FtusAudioProcessor a;
    setDenormalized(a, ftus::ids::cutoff, 2500.0f);
    juce::ValueTree root("FilterTableUS"); // the Phase 0 stub format: binary ValueTree stream
    root.setProperty("stateVersion", 1, nullptr);
    root.appendChild(a.state().copyState(), nullptr);
    juce::MemoryBlock blob;
    {
        juce::MemoryOutputStream out(blob, false);
        root.writeToStream(out);
    }

    ftus::FtusAudioProcessor b;
    b.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
    REQUIRE(rawValue(b, ftus::ids::cutoff) == Approx(2500.0f).epsilon(1e-3));
}

TEST_CASE_METHOD(JuceEnv, "malformed state data is ignored safely", "[state][session]") {
    ftus::FtusAudioProcessor a;
    const float before = rawValue(a, ftus::ids::cutoff);
    a.setStateInformation("garbage!", 8);
    const char* wrongRoot = "<?xml version=\"1.0\"?><NotUs stateVersion=\"1\"/>";
    a.setStateInformation(wrongRoot, static_cast<int>(strlen(wrongRoot)));
    a.setStateInformation(nullptr, 0);
    REQUIRE(rawValue(a, ftus::ids::cutoff) == before);
}
