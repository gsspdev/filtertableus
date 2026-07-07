// FilterTableUS GUI — live filter-response display (plan §5).
// Polls the engine's TripleBuffer<ResponseCurve> on a 30 Hz timer via the processor's engine
// accessor; the path is rebuilt (and the view repainted) ONLY when fresh data arrives.
// Frequency mapping uses ftc::ResponseCurve::frequencyForPoint — the one shared grid.
#pragma once
#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

class SpectrumView : public juce::Component, private juce::Timer {
public:
    explicit SpectrumView(FtusAudioProcessor& processor);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    static constexpr float kMinDb = -30.0f;
    static constexpr float kMaxDb = 30.0f;

    void timerCallback() override;
    void rebuildPaths();
    juce::Rectangle<float> plotArea() const;
    float xForFrequency(float hz, juce::Rectangle<float> r) const;
    float yForDb(float db, juce::Rectangle<float> r) const;

    FtusAudioProcessor& processor_;
    ftc::ResponseCurve curve_{};
    bool hasCurve_ = false;
    juce::Path strokePath_, fillPath_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrumView)
};

} // namespace ftus
