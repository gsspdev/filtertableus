// Wave-3 integration item 7: factory-preset audition through the REAL engine.
//
// A human can't be in this loop, so the renders must prove musical function:
//   - every factory preset loads, regenerates its factory table, and renders a saw without
//     NaN/silence/blowups;
//   - each preset audibly filters (the wet output differs from the latency-aligned dry
//     signal, and the per-harmonic gains it imposes on the saw are strongly non-flat);
//   - presets are mutually distinct (pairwise harmonic-gain signatures differ);
//   - preset-bar semantics hold: load clears the dirty flag, edits set it, prev/next wrap.
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <complex>
#include <map>
#include <vector>

#include <juce_dsp/juce_dsp.h>

#include "IntegrationTestHelpers.h"

using namespace itest;

namespace {

constexpr double kFs = 48000.0;
constexpr double kSawHz = 110.0;
constexpr int kRenderLen = 3 * 48000;
constexpr int kFftOrder = 15; // 32768
constexpr int kFftLen = 1 << kFftOrder;
constexpr int kAnalysisStart = 48000; // skip latency fill + slow-LFO onset
constexpr int kNumHarmonics = 40;     // 110 Hz .. 4.4 kHz probe comb

void fillSaw(juce::AudioBuffer<float>& buf, double freq, float amp = 0.4f) {
    float* d0 = buf.getWritePointer(0);
    for (int n = 0; n < buf.getNumSamples(); ++n) {
        const double ph = std::fmod(freq * n / kFs, 1.0);
        d0[n] = amp * static_cast<float>(2.0 * ph - 1.0);
    }
    for (int ch = 1; ch < buf.getNumChannels(); ++ch)
        buf.copyFrom(ch, 0, buf, 0, 0, buf.getNumSamples());
}

/// Hann-windowed magnitude spectrum of channel 0, samples [start, start + kFftLen).
std::vector<float> magnitudeSpectrum(const juce::AudioBuffer<float>& buf, int start) {
    static juce::dsp::FFT fft(kFftOrder);
    std::vector<float> data(static_cast<size_t>(2 * kFftLen), 0.0f);
    const float* d = buf.getReadPointer(0);
    for (int n = 0; n < kFftLen; ++n) {
        const float w = 0.5f - 0.5f * std::cos(6.2831853f * n / (kFftLen - 1));
        data[static_cast<size_t>(n)] = d[start + n] * w;
    }
    fft.performRealOnlyForwardTransform(data.data(), true);
    std::vector<float> mag(static_cast<size_t>(kFftLen / 2), 0.0f);
    for (int k = 0; k < kFftLen / 2; ++k) {
        const float re = data[static_cast<size_t>(2 * k)];
        const float im = data[static_cast<size_t>(2 * k + 1)];
        mag[static_cast<size_t>(k)] = std::sqrt(re * re + im * im);
    }
    return mag;
}

/// Gain (dB) the preset imposed on each saw harmonic: energy around bin(h*110 Hz) in the
/// output over the same in the input. +-2 bins absorbs modulation smear.
std::vector<float> harmonicGainsDb(const std::vector<float>& outMag,
                                   const std::vector<float>& inMag) {
    std::vector<float> gains;
    for (int h = 1; h <= kNumHarmonics; ++h) {
        const int bin = static_cast<int>(std::round(h * kSawHz * kFftLen / kFs));
        double eo = 0.0, ei = 0.0;
        for (int b = bin - 2; b <= bin + 2; ++b) {
            eo += static_cast<double>(outMag[static_cast<size_t>(b)])
                  * outMag[static_cast<size_t>(b)];
            ei += static_cast<double>(inMag[static_cast<size_t>(b)])
                  * inMag[static_cast<size_t>(b)];
        }
        gains.push_back(static_cast<float>(10.0 * std::log10((eo + 1e-24) / (ei + 1e-24))));
    }
    return gains;
}

struct AuditionResult {
    float rmsDbSecondHalf = -300.0f;
    float peak = 0.0f;
    float wetVsDryDb = -300.0f; // residual vs latency-aligned dry: HIGH = audible filtering
    std::vector<float> gains;   // per-harmonic imposed gain (dB)
};

AuditionResult auditionPreset(const juce::String& name) {
    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    REQUIRE(proc.stateManager().loadPreset(name));
    REQUIRE(proc.stateManager().currentPresetName() == name);
    REQUIRE(proc.engine().currentTableForUi() != nullptr); // factory table regenerated
    proc.prepareToPlay(kFs, 512);

    juce::AudioBuffer<float> in(2, kRenderLen);
    fillSaw(in, kSawHz);
    RenderPlan plan;
    const auto res = renderThroughProcessor(proc, in, plan);
    REQUIRE(allFinite(res.output));
    CHECK(res.hostLatencyConstant);

    AuditionResult a;
    a.rmsDbSecondHalf = rmsDb(res.output, 0, kRenderLen / 2, kRenderLen / 2);
    a.peak = maxAbsSample(res.output);
    a.wetVsDryDb = nullDepthDb(res.output, in, res.hostLatencyAtStart, kAnalysisStart);
    const auto outMag = magnitudeSpectrum(res.output, kAnalysisStart);
    const auto inMag = magnitudeSpectrum(in, kAnalysisStart);
    a.gains = harmonicGainsDb(outMag, inMag);
    return a;
}

} // namespace

TEST_CASE_METHOD(JuceEnv, "w3: every factory preset renders audible, musical filtering",
                 "[wave3][presets]") {
    ftus::FtusAudioProcessor lister;
    const auto names = lister.stateManager().listPresets();
    REQUIRE(names.size() >= 10); // 10 factory presets (user presets may follow)

    std::map<juce::String, std::vector<float>> signatures;
    for (int i = 0; i < 10; ++i) {
        const auto& name = names[i];
        INFO("preset: " << name.toStdString());
        const auto a = auditionPreset(name);

        // Alive: no silence, no blowup.
        {
            INFO("rms(second half) = " << a.rmsDbSecondHalf << " dBFS, peak = " << a.peak);
            CHECK(a.rmsDbSecondHalf > -50.0f);
            CHECK(a.peak < 2.5f);
        }

        // Audible filtering: the wet render must NOT null against the aligned dry saw
        // (a passthrough/dead preset would sit below -40 dB here).
        {
            INFO("wet-vs-dry residual = " << a.wetVsDryDb << " dB");
            CHECK(a.wetVsDryDb > -35.0f);
        }

        // Musical = strongly non-flat: the imposed per-harmonic gains must span a real
        // sculpting range, not a uniform gain change. Bound note: "Linear Air" mixes 50%
        // phase-aligned dry, so its response is |0.5 + 0.5 H| — notches bottom out at -6 dB
        // by design, capping its spread near 6 dB (measured 5.9); every other preset shows
        // 10-60 dB.
        {
            const auto [mn, mx] = std::minmax_element(a.gains.begin(), a.gains.end());
            INFO("harmonic gain spread = " << (*mx - *mn) << " dB over " << kNumHarmonics
                                           << " harmonics");
            CHECK(*mx - *mn > 4.5f);
        }
        signatures[name] = a.gains;
    }

    // Distinctness: every pair of presets imposes a measurably different harmonic signature.
    for (int i = 0; i < 10; ++i)
        for (int j = i + 1; j < 10; ++j) {
            const auto& ga = signatures[names[i]];
            const auto& gb = signatures[names[j]];
            float dist = 0.0f;
            for (size_t k = 0; k < ga.size(); ++k)
                dist = std::max(dist, std::abs(ga[k] - gb[k]));
            INFO("presets '" << names[i].toStdString() << "' vs '" << names[j].toStdString()
                             << "' max harmonic-gain distance = " << dist << " dB");
            CHECK(dist > 1.5f);
        }
}

TEST_CASE_METHOD(JuceEnv, "w3: preset bar semantics — dirty flag and prev/next wrap",
                 "[wave3][presets]") {
    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, kFs, 512);
    auto& sm = proc.stateManager();
    const auto names = sm.listPresets();
    REQUIRE(names.size() >= 10);

    REQUIRE(sm.loadPreset(names[0]));
    CHECK_FALSE(sm.isDirty());

    // Editing any parameter marks the session dirty; reloading clears it.
    setParamReal(proc, ftus::ids::resonance, 0.63f);
    CHECK(sm.isDirty());
    REQUIRE(sm.loadPreset(names[0]));
    CHECK_FALSE(sm.isDirty());

    // next/prev walk the flat list and wrap at both ends.
    REQUIRE(sm.nextPreset());
    CHECK(sm.currentPresetName() == names[1]);
    REQUIRE(sm.prevPreset());
    CHECK(sm.currentPresetName() == names[0]);
    REQUIRE(sm.prevPreset()); // wrap backwards to the last entry
    CHECK(sm.currentPresetName() == names[names.size() - 1]);
    REQUIRE(sm.nextPreset()); // and forwards again
    CHECK(sm.currentPresetName() == names[0]);
}
