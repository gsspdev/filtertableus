// Golden parameter-list test: locks the EXACT host-facing parameter contract (plan §3,
// ftus/PluginIDs.h). Any drift — missing/extra IDs, range/default/skew/choice changes,
// version-hint changes, choice-order vs ftc-enum mismatches — hard-fails here.
// The expected values are DELIBERATELY duplicated in this file (not read from headers)
// so that edits to the frozen headers cannot silently rewrite the contract.
#include <map>
#include <set>
#include <string>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "IntegrationTestHelpers.h"
#include "ftc/Types.h"
#include "ftus/PluginIDs.h"

using Catch::Approx;
using itest::JuceEnv;

namespace {

// --- choice order locks vs the ftc enums (INTERFACES.md rule 7) --------------------------
static_assert(static_cast<int>(ftc::PhaseMode::Minimum) == 0);
static_assert(static_cast<int>(ftc::PhaseMode::Linear) == 1);
static_assert(static_cast<int>(ftc::PhaseMode::Original) == 2);
static_assert(static_cast<int>(ftc::PhaseMode::Raw) == 3);

static_assert(static_cast<int>(ftc::LfoShape::Sine) == 0);
static_assert(static_cast<int>(ftc::LfoShape::Triangle) == 1);
static_assert(static_cast<int>(ftc::LfoShape::SawUp) == 2);
static_assert(static_cast<int>(ftc::LfoShape::SawDown) == 3);
static_assert(static_cast<int>(ftc::LfoShape::Square) == 4);
static_assert(static_cast<int>(ftc::LfoShape::SampleHold) == 5);

static_assert(static_cast<int>(ftc::SyncDivision::W8) == 0);
static_assert(static_cast<int>(ftc::SyncDivision::Quarter) == 7);
static_assert(static_cast<int>(ftc::SyncDivision::ThirtySecond) == 15);
static_assert(static_cast<int>(ftc::SyncDivision::NumDivisions) == 16);

static_assert(ftus::kParamVersionHint == 1);
static_assert(ftus::kNumParameters == 27);

// --- expected golden table ----------------------------------------------------------------
struct FloatSpec {
    const char* id;
    float min, max, def;
    float skewCentre; // < 0 => linear range (0.5 maps to the arithmetic midpoint)
};
struct ChoiceSpec {
    const char* id;
    std::vector<std::string> choices;
    int defaultIndex;
};
struct BoolSpec {
    const char* id;
    bool def;
};

const std::vector<std::string> kPhaseModes{"Minimum", "Linear", "Original", "Raw"};
const std::vector<std::string> kShapes{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "S&H"};
const std::vector<std::string> kDivisions{"8/1",  "4/1",  "2/1",  "1/1",  "1/2",   "1/2T",
                                          "1/4D", "1/4",  "1/4T", "1/8D", "1/8",   "1/8T",
                                          "1/16D", "1/16", "1/16T", "1/32"};

const std::vector<FloatSpec> kFloatSpecs{
    {"scan", 0.0f, 1.0f, 0.0f, -1.0f},
    {"cutoff", 20.0f, 20000.0f, 440.0f, 1000.0f},
    {"resonance", -1.0f, 1.0f, 0.0f, -1.0f},
    {"mix", 0.0f, 1.0f, 1.0f, -1.0f},
    {"keytrack", -1.0f, 1.0f, 0.0f, -1.0f},
    {"outGain", -24.0f, 12.0f, 0.0f, -1.0f},
    {"lfo1Rate", 0.02f, 20.0f, 1.0f, 1.0f},
    {"lfo1ToScan", -1.0f, 1.0f, 0.0f, -1.0f},
    {"lfo1ToCutoff", -1.0f, 1.0f, 0.0f, -1.0f},
    {"lfo2Rate", 0.02f, 20.0f, 0.25f, 1.0f},
    {"lfo2ToScan", -1.0f, 1.0f, 0.0f, -1.0f},
    {"lfo2ToCutoff", -1.0f, 1.0f, 0.0f, -1.0f},
    {"envSens", -24.0f, 24.0f, 0.0f, -1.0f},
    {"envAttack", 0.1f, 500.0f, 10.0f, 10.0f},
    {"envRelease", 1.0f, 2000.0f, 200.0f, 200.0f},
    {"envToScan", -1.0f, 1.0f, 0.0f, -1.0f},
    {"envToCutoff", -1.0f, 1.0f, 0.0f, -1.0f},
};

const std::vector<ChoiceSpec> kChoiceSpecs{
    {"phaseMode", kPhaseModes, 0},
    {"lfo1Div", kDivisions, 7},
    {"lfo1Shape", kShapes, 0},
    {"lfo2Div", kDivisions, 7},
    {"lfo2Shape", kShapes, 0},
};

const std::vector<BoolSpec> kBoolSpecs{
    {"bypass", false}, {"lfo1Sync", false}, {"lfo1Retrig", false},
    {"lfo2Sync", false}, {"lfo2Retrig", false},
};

} // namespace

TEST_CASE_METHOD(JuceEnv, "golden parameter list: exact 27-parameter contract", "[integration][golden]") {
    ftus::FtusAudioProcessor proc;

    // ---- exact ID set: no missing, no extra ---------------------------------------------
    std::set<std::string> expectedIds;
    for (const auto& s : kFloatSpecs) expectedIds.insert(s.id);
    for (const auto& s : kChoiceSpecs) expectedIds.insert(s.id);
    for (const auto& s : kBoolSpecs) expectedIds.insert(s.id);
    REQUIRE(expectedIds.size() == 27);

    std::map<std::string, juce::RangedAudioParameter*> actual;
    for (auto* p : proc.getParameters()) {
        auto* withId = dynamic_cast<juce::RangedAudioParameter*>(p);
        INFO("every hosted parameter must be a RangedAudioParameter with an ID");
        REQUIRE(withId != nullptr);
        REQUIRE(actual.emplace(withId->paramID.toStdString(), withId).second); // no duplicates
    }
    REQUIRE(actual.size() == static_cast<size_t>(ftus::kNumParameters));

    std::set<std::string> actualIds;
    for (const auto& [id, p] : actual) actualIds.insert(id);
    for (const auto& id : expectedIds) {
        INFO("missing parameter: " << id);
        REQUIRE(actualIds.count(id) == 1);
    }
    for (const auto& id : actualIds) {
        INFO("unexpected extra parameter: " << id);
        REQUIRE(expectedIds.count(id) == 1);
    }

    // ---- version hints are permanent (all 1 in v1) --------------------------------------
    for (const auto& [id, p] : actual) {
        INFO("version hint for " << id);
        CHECK(p->getParameterID() == juce::String(id));
        REQUIRE(p->getVersionHint() == ftus::kParamVersionHint);
    }

    // ---- float parameters: type, range, default, skew shape ------------------------------
    for (const auto& spec : kFloatSpecs) {
        INFO("float parameter: " << spec.id);
        auto* f = dynamic_cast<juce::AudioParameterFloat*>(actual.at(spec.id));
        REQUIRE(f != nullptr);
        const auto& range = f->getNormalisableRange();
        REQUIRE(range.start == spec.min);
        REQUIRE(range.end == spec.max);
        REQUIRE(f->get() == Approx(spec.def).margin(1e-4).epsilon(1e-4));

        const float mid = f->convertFrom0to1(0.5f);
        if (spec.skewCentre > 0.0f) {
            INFO(spec.id << " must be skewed so 0.5 -> " << spec.skewCentre);
            REQUIRE(mid == Approx(spec.skewCentre).epsilon(5e-3));
        } else {
            INFO(spec.id << " must be a linear range (0.5 -> midpoint)");
            REQUIRE(mid == Approx((spec.min + spec.max) * 0.5f).margin(1e-3));
        }
    }

    // ---- choice parameters: type, exact strings + order, choice count, default ----------
    for (const auto& spec : kChoiceSpecs) {
        INFO("choice parameter: " << spec.id);
        auto* c = dynamic_cast<juce::AudioParameterChoice*>(actual.at(spec.id));
        REQUIRE(c != nullptr);
        REQUIRE(c->choices.size() == static_cast<int>(spec.choices.size()));
        for (size_t i = 0; i < spec.choices.size(); ++i) {
            INFO(spec.id << " choice " << i);
            REQUIRE(c->choices[static_cast<int>(i)].toStdString() == spec.choices[i]);
        }
        REQUIRE(c->getIndex() == spec.defaultIndex);
    }

    // ---- bool parameters: type + default -------------------------------------------------
    for (const auto& spec : kBoolSpecs) {
        INFO("bool parameter: " << spec.id);
        auto* b = dynamic_cast<juce::AudioParameterBool*>(actual.at(spec.id));
        REQUIRE(b != nullptr);
        REQUIRE(b->get() == spec.def);
    }

    // ---- choice-table sizes lock to the ftc enums ----------------------------------------
    REQUIRE(ftus::phaseModeChoices().size() == 4);
    REQUIRE(ftus::lfoShapeChoices().size() == 6);
    REQUIRE(ftus::syncDivisionChoices().size() == static_cast<int>(ftc::SyncDivision::NumDivisions));
}

TEST_CASE_METHOD(JuceEnv, "golden: bypass is wired and latency reported >= 0", "[integration][golden]") {
    ftus::FtusAudioProcessor proc;

    auto* bypass = proc.getBypassParameter();
    REQUIRE(bypass != nullptr);
    auto* bypassWithId = dynamic_cast<juce::AudioProcessorParameterWithID*>(bypass);
    REQUIRE(bypassWithId != nullptr);
    REQUIRE(bypassWithId->paramID == juce::String(ftus::ids::bypass));
    REQUIRE(dynamic_cast<juce::AudioParameterBool*>(bypass) != nullptr);
    REQUIRE(bypass == proc.state().getParameter(ftus::ids::bypass));

    proc.setPlayConfigDetails(2, 2, 48000.0, 512);
    proc.prepareToPlay(48000.0, 512);
    REQUIRE(proc.getLatencySamples() >= 0);
    REQUIRE(proc.engine().latencySamples() >= 0);
}
