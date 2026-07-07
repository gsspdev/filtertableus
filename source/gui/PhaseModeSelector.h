// FilterTableUS GUI — 4-segment phase-mode selector (Minimum/Linear/Original/Raw) with an
// accent underline on the active segment; bound through a raw juce::ParameterAttachment.
// Tooltips note the added latency of the Linear/Original modes.
#pragma once
#include "gui/SegmentButton.h"
#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

class PhaseModeSelector : public juce::Component {
public:
    PhaseModeSelector(FtusAudioProcessor& processor, BindingRegistry& registry,
                      ReadoutSink* sink);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setSelected(int index);

    juce::RangedAudioParameter* param_ = nullptr;
    std::unique_ptr<juce::ParameterAttachment> attachment_;
    juce::OwnedArray<SegmentButton> segments_;
    ReadoutSink* sink_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PhaseModeSelector)
};

} // namespace ftus
