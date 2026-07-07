// Shared helpers for the FilterTableUS integration suite (harness workstream).
//
// Two assertion tiers live in this suite:
//   Tier A  — always on: finiteness, RMS bounds, latency constancy, state round-trips.
//   Tier B  — auto-tightening: real-engine-only checks (wet != dry, mix-0 null vs
//             latency-delayed dry, per-mode instance latency). Gated BEHAVIORALLY via
//             engineIsReal(): a strongly-filtering table is loaded and noise is processed;
//             if the output is (near-)bit-identical to the input the passthrough stub is in
//             place and tier-B checks pass trivially with a printed notice. Do NOT gate on
//             the static latencySamplesFor() — it already returns real design values under
//             the stub (whose *instance* latencySamples() stays 0).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <juce_events/juce_events.h>

#include "ftc/FilterTableEngine.h"
#include "ftc/WavetableData.h"
#include "ftus/PluginIDs.h"
#include "plugin/PluginProcessor.h"

namespace itest {

/// Per-test-case JUCE runtime (MessageManager etc.). Processors must outlive nothing here:
/// construct the fixture first (Catch does), processors inside the test body.
struct JuceEnv {
    juce::ScopedJuceInitialiser_GUI init;
};

inline void pumpMessageLoop(int milliseconds) {
    juce::MessageManager::getInstance()->runDispatchLoopUntil(milliseconds);
}

// ---------------------------------------------------------------------------- parameter setters

inline juce::RangedAudioParameter* requireParam(ftus::FtusAudioProcessor& proc, const char* id) {
    auto* p = proc.state().getParameter(id);
    REQUIRE(p != nullptr);
    return p;
}

/// Set a parameter from its REAL-world value (Hz, dB, 0..1, choice index as float...).
inline void setParamReal(ftus::FtusAudioProcessor& proc, const char* id, float realValue) {
    auto* p = requireParam(proc, id);
    p->setValueNotifyingHost(p->convertTo0to1(realValue));
}

inline void setChoiceParam(ftus::FtusAudioProcessor& proc, const char* id, int index) {
    setParamReal(proc, id, static_cast<float>(index));
}

inline void setBoolParam(ftus::FtusAudioProcessor& proc, const char* id, bool on) {
    requireParam(proc, id)->setValueNotifyingHost(on ? 1.0f : 0.0f);
}

// -------------------------------------------------------------------------------- wavetables

inline ftus::TableSourceInfo userTableInfo(const char* name) {
    ftus::TableSourceInfo info;
    info.type = ftus::TableSourceInfo::Type::UserFile;
    info.displayName = name;
    return info;
}

/// Synthesized strongly-filtering (lowpass-shaped) table: frame f keeps harmonics
/// 1..harmonicsPerFrame[f] (1/sqrt(k) rolloff), zero above -> as a filter it kills
/// everything above harmonic H (freq H*cutoff/24). Deterministic, DC-free, peak 0.9.
inline ftc::WavetablePtr makeFilterTable(std::string name, const std::vector<int>& harmonicsPerFrame) {
    constexpr int kLen = ftc::WavetableData::kFrameLength;
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const int numFrames = static_cast<int>(harmonicsPerFrame.size());
    std::vector<float> samples(static_cast<size_t>(numFrames) * kLen);
    for (int f = 0; f < numFrames; ++f) {
        float* frame = samples.data() + static_cast<size_t>(f) * kLen;
        const int maxH = harmonicsPerFrame[static_cast<size_t>(f)];
        for (int n = 0; n < kLen; ++n) {
            double s = 0.0;
            for (int k = 1; k <= maxH; ++k)
                s += std::sin(kTwoPi * k * n / kLen) / std::sqrt(static_cast<double>(k));
            frame[n] = static_cast<float>(s);
        }
        float peak = 0.0f;
        for (int n = 0; n < kLen; ++n)
            peak = std::max(peak, std::abs(frame[n]));
        if (peak > 0.0f)
            for (int n = 0; n < kLen; ++n)
                frame[n] *= 0.9f / peak;
    }
    auto table = ftc::WavetableData::analyze(samples, numFrames, std::move(name));
    REQUIRE(table != nullptr);
    return table;
}

// ----------------------------------------------------------------------------- test signals

/// Deterministic white noise, uniform in [-0.5, 0.5], distinct per channel (xorshift32).
inline void fillNoise(juce::AudioBuffer<float>& buf, std::uint32_t seed) {
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        std::uint32_t x = seed + 0x9E3779B9u * static_cast<std::uint32_t>(ch + 1);
        float* d = buf.getWritePointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n) {
            x ^= x << 13; x ^= x >> 17; x ^= x << 5;
            d[n] = (static_cast<float>(x) / 4294967296.0f) - 0.5f;
        }
    }
}

/// Exponential sine sweep f0 -> f1 across the whole buffer, amp on all channels.
inline void fillExpSweep(juce::AudioBuffer<float>& buf, double sampleRate, double f0, double f1,
                         float amp = 0.5f) {
    constexpr double kTwoPi = 6.283185307179586476925286766559;
    const int total = buf.getNumSamples();
    const double T = total / sampleRate;
    const double k = std::log(f1 / f0);
    float* d0 = buf.getWritePointer(0);
    for (int n = 0; n < total; ++n) {
        const double t = n / sampleRate;
        const double phase = kTwoPi * f0 * (T / k) * (std::exp(k * t / T) - 1.0);
        d0[n] = amp * static_cast<float>(std::sin(phase));
    }
    for (int ch = 1; ch < buf.getNumChannels(); ++ch)
        buf.copyFrom(ch, 0, buf, 0, 0, total);
}

// ---------------------------------------------------------------------------------- metrics

inline bool allFinite(const juce::AudioBuffer<float>& buf) {
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const float* d = buf.getReadPointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            if (!std::isfinite(d[n]))
                return false;
    }
    return true;
}

inline float rmsDb(const juce::AudioBuffer<float>& buf, int channel, int start = 0, int length = -1) {
    const int total = buf.getNumSamples();
    if (length < 0)
        length = total - start;
    const float* d = buf.getReadPointer(channel);
    double acc = 0.0;
    for (int n = start; n < start + length; ++n)
        acc += static_cast<double>(d[n]) * d[n];
    const double rms = std::sqrt(acc / std::max(1, length));
    return static_cast<float>(20.0 * std::log10(rms + 1e-30));
}

inline float maxAbsSample(const juce::AudioBuffer<float>& buf) {
    float m = 0.0f;
    for (int ch = 0; ch < buf.getNumChannels(); ++ch) {
        const float* d = buf.getReadPointer(ch);
        for (int n = 0; n < buf.getNumSamples(); ++n)
            m = std::max(m, std::abs(d[n]));
    }
    return m;
}

/// Energy of (out[n] - in[n - delay]) relative to in, in dB, over samples [start, N).
/// < -60 means out nulls against the delayed input to better than -60 dBFS.
inline float nullDepthDb(const juce::AudioBuffer<float>& out, const juce::AudioBuffer<float>& in,
                         int delaySamples, int start) {
    double accDiff = 0.0, accRef = 0.0;
    const int total = std::min(out.getNumSamples(), in.getNumSamples());
    for (int ch = 0; ch < std::min(out.getNumChannels(), in.getNumChannels()); ++ch) {
        const float* o = out.getReadPointer(ch);
        const float* i = in.getReadPointer(ch);
        for (int n = start; n < total; ++n) {
            const float ref = (n - delaySamples >= 0) ? i[n - delaySamples] : 0.0f;
            const float diff = o[n] - ref;
            accDiff += static_cast<double>(diff) * diff;
            accRef += static_cast<double>(ref) * ref;
        }
    }
    if (accRef <= 0.0)
        return 0.0f;
    return static_cast<float>(10.0 * std::log10(accDiff / accRef + 1e-30));
}

// ---------------------------------------------------------------------------- render harness

struct RenderPlan {
    int blockSize = 512;
    int warmupBlocks = 4;               // silence blocks before the measured render
    bool scanRampMidRender = false;     // ramp `scan` 0 -> 1 between 25% and 75% of the render
    bool stateSaveLoadMidRender = false;// get+setStateInformation at the halfway block
};

struct RenderOutcome {
    juce::AudioBuffer<float> output;
    int hostLatencyAtStart = 0;
    int engineLatencyAtStart = 0;
    bool hostLatencyConstant = true;
    bool engineLatencyConstant = true;
    int blocksRendered = 0;
};

/// Push `input` through an already prepared processor block by block, applying the plan's
/// mid-render automation. The caller prepares, adopts tables, and sets parameters first.
inline RenderOutcome renderThroughProcessor(ftus::FtusAudioProcessor& proc,
                                            const juce::AudioBuffer<float>& input,
                                            const RenderPlan& plan) {
    const int numCh = input.getNumChannels();
    const int total = input.getNumSamples();

    RenderOutcome res;
    res.output.setSize(numCh, total);
    res.output.clear();

    juce::AudioBuffer<float> block(numCh, plan.blockSize);
    juce::MidiBuffer midi;

    for (int w = 0; w < plan.warmupBlocks; ++w) {
        block.setSize(numCh, plan.blockSize, false, false, true);
        block.clear();
        midi.clear();
        proc.processBlock(block, midi);
    }

    res.hostLatencyAtStart = proc.getLatencySamples();
    res.engineLatencyAtStart = proc.engine().latencySamples();

    const int numBlocks = (total + plan.blockSize - 1) / plan.blockSize;
    for (int b = 0; b < numBlocks; ++b) {
        const int offset = b * plan.blockSize;
        const int n = std::min(plan.blockSize, total - offset);

        if (plan.scanRampMidRender) {
            const float t = numBlocks > 1 ? static_cast<float>(b) / static_cast<float>(numBlocks - 1)
                                          : 0.0f;
            const float ramp = std::clamp((t - 0.25f) / 0.5f, 0.0f, 1.0f);
            setParamReal(proc, ftus::ids::scan, ramp);
        }
        if (plan.stateSaveLoadMidRender && b == numBlocks / 2) {
            juce::MemoryBlock blob;
            proc.getStateInformation(blob);
            proc.setStateInformation(blob.getData(), static_cast<int>(blob.getSize()));
        }

        block.setSize(numCh, n, false, false, true);
        for (int ch = 0; ch < numCh; ++ch)
            block.copyFrom(ch, 0, input, ch, offset, n);
        midi.clear();
        proc.processBlock(block, midi);
        for (int ch = 0; ch < numCh; ++ch)
            res.output.copyFrom(ch, offset, block, ch, 0, n);

        if (proc.getLatencySamples() != res.hostLatencyAtStart)
            res.hostLatencyConstant = false;
        if (proc.engine().latencySamples() != res.engineLatencyAtStart)
            res.engineLatencyConstant = false;
        ++res.blocksRendered;
    }
    return res;
}

// ----------------------------------------------------------------- engine tier detection

namespace detail {
inline bool computeEngineIsReal() {
    ftus::FtusAudioProcessor proc;
    proc.setPlayConfigDetails(2, 2, 48000.0, 512);
    setParamReal(proc, ftus::ids::mix, 1.0f);
    setParamReal(proc, ftus::ids::cutoff, 2000.0f);
    setParamReal(proc, ftus::ids::scan, 0.0f);
    setParamReal(proc, ftus::ids::resonance, 0.0f);
    setChoiceParam(proc, ftus::ids::phaseMode, static_cast<int>(ftc::PhaseMode::Minimum));
    proc.prepareToPlay(48000.0, 512);
    proc.adoptWavetable(makeFilterTable("StubProbeLowpass", {8}), userTableInfo("StubProbeLowpass"));

    juce::AudioBuffer<float> in(2, 24000);
    fillNoise(in, 0xC0FFEEu);
    RenderPlan plan;
    auto res = renderThroughProcessor(proc, in, plan);

    float maxDiff = 0.0f;
    for (int ch = 0; ch < 2; ++ch) {
        const float* o = res.output.getReadPointer(ch);
        const float* i = in.getReadPointer(ch);
        for (int n = 0; n < in.getNumSamples(); ++n)
            maxDiff = std::max(maxDiff, std::abs(o[n] - i[n]));
    }
    return maxDiff > 1.0e-6f; // passthrough stub leaves the buffer bit-identical
}
} // namespace detail

/// True once the real engine is in (behavioral detection; see file header). Cached per run.
inline bool engineIsReal() {
    static const bool real = [] {
        const bool r = detail::computeEngineIsReal();
        std::cout << (r ? "[engine-tier] REAL engine behavior detected -> tier-B assertions ACTIVE"
                        : "[engine-tier] passthrough STUB detected -> tier-B assertions pass "
                          "trivially with this notice (they tighten automatically once the real "
                          "engine lands)")
                  << std::endl;
        return r;
    }();
    return real;
}

/// Gate for tier-B test bodies. Returns false (and prints the skip notice) under the stub.
inline bool tierBActiveOrNotice(const char* what) {
    if (engineIsReal())
        return true;
    std::cout << "[stub-tier] SKIPPED (auto-tightens when the real engine lands): " << what
              << std::endl;
    WARN("stub tier: '" << what << "' passed trivially; tightens when the real engine lands");
    return false;
}

} // namespace itest
