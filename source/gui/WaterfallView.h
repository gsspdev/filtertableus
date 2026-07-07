// FilterTableUS GUI — pseudo-3D wavetable waterfall (plan §5).
// Frames are drawn back-to-front as stacked decimated polylines; the active frame (from the
// engine's post-modulation currentScan atomic, polled on a timer) is drawn last in accent.
// Click/vertical-drag drives the `scan` parameter through a raw juce::ParameterAttachment
// with proper begin/end gestures (shift = fine, double-click = reset).
#pragma once
#include <vector>

#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

class WaterfallView : public juce::Component, private juce::Timer {
public:
    WaterfallView(FtusAudioProcessor& processor, BindingRegistry& registry, ReadoutSink* sink);

    void paint(juce::Graphics&) override;
    void resized() override;

    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    static constexpr int kMaxDepths = 48;      // <=48 evenly-strided frames
    static constexpr int kPointsPerFrame = 128; // decimation per polyline
    static constexpr float kDx = 2.4f;          // per-depth offset, design px
    static constexpr float kDy = -4.6f;

    struct Geometry {
        float x0 = 0.0f, baseY = 0.0f, waveWidth = 0.0f, amplitude = 0.0f;
        int depths = 1;
    };

    void timerCallback() override;
    void rebuildPaths();
    Geometry computeGeometry(int depths) const;
    juce::Path buildFramePath(int frameIndex, float depthSlot) const;
    float scanForPosition(juce::Point<float> pos) const;
    float displayScan() const;
    juce::String scanText(float scan01) const;
    void pushReadout(float scan01);

    FtusAudioProcessor& processor_;
    ReadoutSink* sink_ = nullptr;

    std::unique_ptr<juce::ParameterAttachment> scanAttachment_;
    juce::RangedAudioParameter* scanParam_ = nullptr;
    float paramValue_ = 0.0f; // last value seen through the attachment (message thread)
    bool paramDirty_ = false;

    ftc::WavetablePtr table_;
    std::vector<juce::Path> framePaths_;
    Geometry geo_;
    float paintedScan_ = -1.0f;

    float dragStartValue_ = 0.0f;
    juce::Point<float> dragStartPos_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaterfallView)
};

} // namespace ftus
