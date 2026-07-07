// StateManagerImpl preset tests: factory list/load, user save/load, next/prev wrap,
// dirty tracking, embedded tables in user presets. User presets are redirected to a temp
// directory via the FTUS_PRESET_DIR override (read lazily by the implementation).
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <vector>

#include <juce_events/juce_events.h>

#include "ftc/WavetableData.h"
#include "ftus/PluginIDs.h"
#include "plugin/PluginProcessor.h"

using Catch::Approx;

namespace {

const juce::StringArray kFactoryNames{
    "Analog Sweep Pad", "Synced Sweep Bass", "Auto Wah",       "Spectral Drifter",
    "Linear Air",       "Raw Mangler",       "Comb Runner",    "Keytrack Formants",
    "Random Steps",     "PWM Drift"};

struct PresetDirFixture {
    juce::ScopedJuceInitialiser_GUI init;
    juce::File dir;

    PresetDirFixture() {
        dir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                  .getChildFile("ftus_state_tests")
                  .getChildFile(juce::Uuid().toString());
        dir.createDirectory();
        setenv("FTUS_PRESET_DIR", dir.getFullPathName().toRawUTF8(), 1);
    }

    ~PresetDirFixture() {
        unsetenv("FTUS_PRESET_DIR");
        dir.deleteRecursively();
    }

    static void setDenormalized(ftus::FtusAudioProcessor& p, const char* id, float value) {
        auto* param = p.state().getParameter(id);
        REQUIRE(param != nullptr);
        param->setValueNotifyingHost(param->convertTo0to1(value));
    }

    static float rawValue(ftus::FtusAudioProcessor& p, const char* id) {
        return p.state().getRawParameterValue(id)->load();
    }
};

} // namespace

TEST_CASE_METHOD(PresetDirFixture, "factory presets are listed first, in order, and all load",
                 "[state][presets]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();
    const auto names = sm.listPresets();
    REQUIRE(names.size() == kFactoryNames.size()); // temp user dir is empty
    for (int i = 0; i < kFactoryNames.size(); ++i)
        REQUIRE(names[i] == kFactoryNames[i]);

    for (const auto& name : kFactoryNames) {
        INFO("loading factory preset " << name);
        REQUIRE(sm.loadPreset(name));
        REQUIRE(sm.currentPresetName() == name);
        REQUIRE(!sm.isDirty());
        REQUIRE(proc.currentTableInfo().type == ftus::TableSourceInfo::Type::Factory);
        REQUIRE(proc.engine().currentTableForUi() != nullptr); // regenerated + adopted
    }

    // Spot-check one preset's parameter payload.
    REQUIRE(sm.loadPreset("Auto Wah"));
    REQUIRE(rawValue(proc, ftus::ids::envSens) == Approx(9.0f).margin(1e-4));
    REQUIRE(rawValue(proc, ftus::ids::envToScan) == Approx(0.7f).margin(1e-4));
    REQUIRE(rawValue(proc, ftus::ids::cutoff) == Approx(380.0f).epsilon(1e-3));
    REQUIRE(proc.currentTableInfo().factoryId == "vowelMorph");
    REQUIRE(sm.loadPreset("Raw Mangler"));
    REQUIRE(static_cast<int>(rawValue(proc, ftus::ids::phaseMode)) == 3); // Raw
    REQUIRE(static_cast<int>(rawValue(proc, ftus::ids::lfo1Shape)) == 5); // S&H
}

TEST_CASE_METHOD(PresetDirFixture, "save -> appears in list -> load restores params",
                 "[state][presets]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();
    setDenormalized(proc, ftus::ids::scan, 0.6f);
    setDenormalized(proc, ftus::ids::resonance, 0.55f);
    REQUIRE(sm.saveUserPreset("My Test"));
    REQUIRE(sm.currentPresetName() == "My Test");
    REQUIRE(!sm.isDirty());
    REQUIRE(dir.getChildFile("My Test.ftpreset").existsAsFile());

    const auto names = sm.listPresets();
    REQUIRE(names.size() == kFactoryNames.size() + 1);
    REQUIRE(names[names.size() - 1] == "My Test"); // user presets come after factory

    setDenormalized(proc, ftus::ids::scan, 0.1f);
    setDenormalized(proc, ftus::ids::resonance, -0.9f);
    REQUIRE(sm.loadPreset("My Test"));
    REQUIRE(rawValue(proc, ftus::ids::scan) == Approx(0.6f).margin(1e-4));
    REQUIRE(rawValue(proc, ftus::ids::resonance) == Approx(0.55f).margin(1e-4));
    REQUIRE(!sm.isDirty());

    // Overwrite is allowed and picks up the new values.
    setDenormalized(proc, ftus::ids::scan, 0.33f);
    REQUIRE(sm.saveUserPreset("My Test"));
    setDenormalized(proc, ftus::ids::scan, 0.9f);
    REQUIRE(sm.loadPreset("My Test"));
    REQUIRE(rawValue(proc, ftus::ids::scan) == Approx(0.33f).margin(1e-4));
}

TEST_CASE_METHOD(PresetDirFixture, "next/prev preset wrap around the flat list",
                 "[state][presets]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();
    const auto names = sm.listPresets();
    REQUIRE(names.size() >= 2);

    REQUIRE(sm.loadPreset(names[0]));
    REQUIRE(sm.prevPreset()); // wraps backwards to the last preset
    REQUIRE(sm.currentPresetName() == names[names.size() - 1]);
    REQUIRE(sm.nextPreset()); // and forwards back to the first
    REQUIRE(sm.currentPresetName() == names[0]);
    REQUIRE(sm.nextPreset());
    REQUIRE(sm.currentPresetName() == names[1]);

    // With no preset loaded yet, next starts at the first entry.
    ftus::FtusAudioProcessor fresh;
    REQUIRE(fresh.stateManager().nextPreset());
    REQUIRE(fresh.stateManager().currentPresetName() == names[0]);
}

TEST_CASE_METHOD(PresetDirFixture, "dirty flag: set on param change, cleared on load and save",
                 "[state][presets][dirty]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();
    REQUIRE(sm.loadPreset("Analog Sweep Pad"));
    REQUIRE(!sm.isDirty());
    setDenormalized(proc, ftus::ids::resonance, 0.8f);
    REQUIRE(sm.isDirty());
    REQUIRE(sm.loadPreset("Analog Sweep Pad"));
    REQUIRE(!sm.isDirty());
    setDenormalized(proc, ftus::ids::mix, 0.25f);
    REQUIRE(sm.isDirty());
    REQUIRE(sm.saveUserPreset("Dirty Check"));
    REQUIRE(!sm.isDirty());
}

TEST_CASE_METHOD(PresetDirFixture, "factory presets are read-only; unknown presets fail",
                 "[state][presets]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();
    REQUIRE(!sm.saveUserPreset("Auto Wah")); // collides with a factory name
    REQUIRE(!sm.loadPreset("Does Not Exist"));
    REQUIRE(!sm.saveUserPreset(""));
}

TEST_CASE_METHOD(PresetDirFixture, "user presets embed the current user table",
                 "[state][presets][wavetable]") {
    ftus::FtusAudioProcessor proc;
    auto& sm = proc.stateManager();

    const int frames = 2;
    std::vector<float> samples(static_cast<size_t>(frames) *
                               ftc::WavetableData::kFrameLength);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = static_cast<float>((i * 31) % 1024) / 512.0f - 1.0f;
    auto table = ftc::WavetableData::analyze(samples, frames, "preset-table");
    REQUIRE(table != nullptr);
    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::Converted;
    info.displayName = "preset-table";
    proc.adoptWavetable(table, info);

    REQUIRE(sm.saveUserPreset("With Table"));

    ftus::FtusAudioProcessor other;
    REQUIRE(other.stateManager().loadPreset("With Table"));
    const auto restored = other.engine().currentTableForUi();
    REQUIRE(restored != nullptr);
    REQUIRE(restored->numFrames() == frames);
    REQUIRE(other.currentTableInfo().type == ftus::TableSourceInfo::Type::Converted);
    for (int f = 0; f < frames; ++f) {
        const auto a = table->frame(f);
        const auto b = restored->frame(f);
        for (size_t i = 0; i < a.size(); ++i)
            REQUIRE(a[i] == b[i]);
    }
}
