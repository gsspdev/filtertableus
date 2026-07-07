// Golden v1 session-state stability test. The blob below was saved by the v1 implementation
// (2026-07-07); every future version must keep loading it exactly (stateVersion 1 back-compat
// lock). Do NOT regenerate/replace this blob for schema changes — add a NEW golden for the new
// version and a migration path instead.
//
// The hidden "[.golden-gen]" test at the bottom documents how the blob was produced.
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <cstring>
#include <vector>

#include <juce_events/juce_events.h>

#include "ftc/WavetableData.h"
#include "ftus/PluginIDs.h"
#include "plugin/PluginProcessor.h"

using Catch::Approx;

namespace {

struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

/// Bit-exact reproducible golden frames (pure integer formula; matches the embedded payload).
std::vector<float> goldenFrames() {
    constexpr int frames = 2;
    std::vector<float> s(static_cast<size_t>(frames) * ftc::WavetableData::kFrameLength);
    for (int f = 0; f < frames; ++f)
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

// Saved by StateManagerImpl v1 on 2026-07-07 (see the [.golden-gen] test).
constexpr const char* kGoldenV1 = R"ftus(<?xml version="1.0" encoding="UTF-8"?>

<FilterTableUS stateVersion="1" pluginVersion="0.1.0">
  <PARAMS guiScale="1.25">
    <PARAM id="bypass" value="0.0"/>
    <PARAM id="cutoff" value="1234.5"/>
    <PARAM id="envAttack" value="24.99999809265137"/>
    <PARAM id="envRelease" value="199.9999542236328"/>
    <PARAM id="envSens" value="0.0"/>
    <PARAM id="envToCutoff" value="0.0"/>
    <PARAM id="envToScan" value="0.0"/>
    <PARAM id="keytrack" value="0.0"/>
    <PARAM id="lfo1Div" value="7.0"/>
    <PARAM id="lfo1Rate" value="0.9999999403953552"/>
    <PARAM id="lfo1Retrig" value="0.0"/>
    <PARAM id="lfo1Shape" value="4.0"/>
    <PARAM id="lfo1Sync" value="0.0"/>
    <PARAM id="lfo1ToCutoff" value="0.0"/>
    <PARAM id="lfo1ToScan" value="0.0"/>
    <PARAM id="lfo2Div" value="7.0"/>
    <PARAM id="lfo2Rate" value="0.2499999552965164"/>
    <PARAM id="lfo2Retrig" value="0.0"/>
    <PARAM id="lfo2Shape" value="0.0"/>
    <PARAM id="lfo2Sync" value="0.0"/>
    <PARAM id="lfo2ToCutoff" value="0.0"/>
    <PARAM id="lfo2ToScan" value="0.0"/>
    <PARAM id="mix" value="1.0"/>
    <PARAM id="outGain" value="7.152557373046875e-7"/>
    <PARAM id="phaseMode" value="1.0"/>
    <PARAM id="resonance" value="-0.25"/>
    <PARAM id="scan" value="0.4199999868869781"/>
  </PARAMS>
  <WAVETABLE type="user" factoryId="" path="/tmp/golden.wav" name="golden-table"
             frames="2" encoding="gzip-f32le-v1" data="978.3wY6W2+ZNuGGGG+KFl6m6uaXXXXXteXXXXXXt+tg496FFFFGmikjVRZIokjVRZIIII433rjjjjjjjzRRRRRRKIcd78eCmOqd7a6Wd8tqqOO6JJpr+MJ5.ra1FqmUxhYtjKShwxvIc5CcmNRRzDpOe+tQQefWwi4tbMt.mliQwrPxjjIhG9OQQUPFT1chhxNqnnTFeTT4TC0QmlPTzHHeJhixonJtJ2gGwK487Mp2DihRjVQGnajJCjgwXHalNygEQArN1J6hR4vbP6aOTDafUwRHelASlwwHXPzW5AchVSSoATm89QdMOgZ35bQNCkSIrTxhTHAdpaSkjMU4FE4Fkj6RYbK9Bsylyf7XybDpfyyU317PdAuiuRjs0XZIsmjo2L.FJYxDYZLaVHqf0xVnX1O+E+g8UBamMxpYoLOlISgrXjLX5GoPmoMzLRfeXueh2DuatG2fKQkbbJkBHaRkD4kw2FxmZbiJab1m6RwbM9.IYyoStrdJiSx43xbKd.Om2xW3WzH6qEzN5J8h9SFLZl.Sk7XArbVCalcx93O4P12dYGrIJjkw7YVjCimQwPHM5Icg1Ryog7S68yTKOi6yMoZNKmfCQgjCoQyi++caplBo13OG4uZ8YoBcaplZo41aZjCExg3DbVplax84YTKeleRCsulSaoKzSRigvnX7jCyh4yxnP1D6f8FeehuS129XmrYVCKmEPdLUl.ilLn+zK5JsiVPi3W16W3s7bd.2hKy43jTFqmbIcRJ9ML2lqQwDE+lze6yStQ461TEujDs2TIaJfR43TIWhav83o7F9D+fDrulQanyjB8iAyHIKlByj4wRY0rQ1NkD+8r3uuYe6mhYKrVVAKjYyzXhjICkAPuIYZOsjFSDe0leGuH9sXtMWgySEbD1L4QFzt36p6xshuMjT7cxmkpxMJa2lJ4ojf8lBYwRoDJmyvE45TCOgWyGoNZf80TZMchdPeYPLBFGSlYP9rDVEafhXObPNrsUJ6hsx5n.VDygoS1LFFFCjToazAZEIR83a176ieigGwc3pTEmhiRQjOifNQcwuCQ4jR7a2wuI4FkgaSE7Phr2jISVHEyw3zbAtF2kGyq3C7cpu80DRhNR2oOjNCmwxjHWlKKlUx5YaraN.g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en++6W+OzzBMsPSKzzBMsPS62klV32zF9Msg9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9en+G5+g9++e6++G.8my1d"/>
  <GUI scale="1.25"/>
  <PRESET name="Init" dirty="1"/>
</FilterTableUS>
)ftus";

} // namespace

TEST_CASE_METHOD(JuceEnv, "a saved v1 golden state blob keeps loading", "[state][golden]") {
    ftus::FtusAudioProcessor p;
    p.setStateInformation(kGoldenV1, static_cast<int>(std::strlen(kGoldenV1)));

    auto raw = [&p](const char* id) { return p.state().getRawParameterValue(id)->load(); };
    REQUIRE(raw(ftus::ids::scan) == Approx(0.42f).margin(1e-4));
    REQUIRE(raw(ftus::ids::cutoff) == Approx(1234.5f).epsilon(1e-3));
    REQUIRE(raw(ftus::ids::resonance) == Approx(-0.25f).margin(1e-5));
    REQUIRE(static_cast<int>(raw(ftus::ids::phaseMode)) == 1); // Linear
    REQUIRE(static_cast<int>(raw(ftus::ids::lfo1Shape)) == 4); // Square
    REQUIRE(raw(ftus::ids::envAttack) == Approx(25.0f).epsilon(1e-3));
    REQUIRE(raw(ftus::ids::mix) == Approx(1.0f).margin(1e-6));

    REQUIRE(static_cast<double>(p.state().state.getProperty("guiScale", 0.0)) == Approx(1.25));
    REQUIRE(p.stateManager().currentPresetName() == "Init");
    REQUIRE(p.stateManager().isDirty()); // the golden session was saved mid-edit

    REQUIRE(p.currentTableInfo().type == ftus::TableSourceInfo::Type::UserFile);
    REQUIRE(p.currentTableInfo().displayName == "golden-table");
    const auto table = p.engine().currentTableForUi();
    REQUIRE(table != nullptr);
    REQUIRE(table->numFrames() == 2);
    const auto expected = goldenFrames();
    for (int f = 0; f < 2; ++f) {
        const auto frame = table->frame(f);
        for (size_t i = 0; i < frame.size(); ++i)
            REQUIRE(frame[i] ==
                    expected[static_cast<size_t>(f) * ftc::WavetableData::kFrameLength + i]);
    }
}

// -------------------------------------------------------------------------------------------
// Hidden generator (run manually: ftus_state_tests "[.golden-gen]"): documents how kGoldenV1
// was produced. Only for authoring NEW goldens for FUTURE state versions.
// -------------------------------------------------------------------------------------------
TEST_CASE_METHOD(JuceEnv, "generate golden state blob (manual)", "[.golden-gen]") {
    ftus::FtusAudioProcessor a;
    setDenormalized(a, ftus::ids::scan, 0.42f);
    setDenormalized(a, ftus::ids::cutoff, 1234.5f);
    setDenormalized(a, ftus::ids::resonance, -0.25f);
    setDenormalized(a, ftus::ids::phaseMode, 1.0f); // Linear
    setDenormalized(a, ftus::ids::lfo1Shape, 4.0f); // Square
    setDenormalized(a, ftus::ids::envAttack, 25.0f);
    a.state().state.setProperty("guiScale", 1.25, nullptr);

    const auto samples = goldenFrames();
    auto table = ftc::WavetableData::analyze(samples, 2, "golden-table");
    REQUIRE(table != nullptr);
    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::UserFile;
    info.path = "/tmp/golden.wav";
    info.displayName = "golden-table";
    a.adoptWavetable(table, info);

    juce::MemoryBlock blob;
    a.getStateInformation(blob);
    const auto text = juce::String::fromUTF8(static_cast<const char*>(blob.getData()),
                                             static_cast<int>(blob.getSize()));
    std::printf("GOLDEN-BEGIN\n%s\nGOLDEN-END\n", text.toRawUTF8());
    juce::File("/tmp/ftus_golden_v1.xml").replaceWithText(text);
}
