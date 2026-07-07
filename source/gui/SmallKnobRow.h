// FilterTableUS GUI — right-column small knobs: Keytrack (bipolar) + Output.
#pragma once
#include "gui/LabeledKnob.h"
#include "gui/Theme.h"
#include "ftus/PluginIDs.h"

namespace ftus {

class SmallKnobRow : public juce::Component {
public:
    SmallKnobRow(juce::AudioProcessorValueTreeState& apvts, BindingRegistry& registry,
                 ReadoutSink* sink)
        : keytrack_(apvts, ids::keytrack, "Keytrack", 46, true, sink, registry),
          output_(apvts, ids::outGain, "Output", 46, false, sink, registry) {
        addAndMakeVisible(keytrack_);
        addAndMakeVisible(output_);
    }

    void paint(juce::Graphics& g) override {
        theme::paintPanel(g, getLocalBounds().toFloat(), 5.0f);
    }

    void resized() override {
        auto r = getLocalBounds().reduced(8, 3);
        const int cellW = r.getWidth() / 2;
        keytrack_.setBounds(r.removeFromLeft(cellW));
        output_.setBounds(r);
    }

private:
    LabeledKnob keytrack_, output_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SmallKnobRow)
};

} // namespace ftus
