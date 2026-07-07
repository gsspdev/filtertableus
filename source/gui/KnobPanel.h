// FilterTableUS GUI — main knob row: Scan / Cutoff / Resonance / Mix (88 px rotaries).
// Resonance is bipolar (center-notched, fill from 12 o'clock).
#pragma once
#include "gui/LabeledKnob.h"
#include "gui/Theme.h"
#include "ftus/PluginIDs.h"

namespace ftus {

class KnobPanel : public juce::Component {
public:
    KnobPanel(juce::AudioProcessorValueTreeState& apvts, BindingRegistry& registry,
              ReadoutSink* sink)
        : scan_(apvts, ids::scan, "Scan", 88, false, sink, registry),
          cutoff_(apvts, ids::cutoff, "Cutoff", 88, false, sink, registry),
          resonance_(apvts, ids::resonance, "Resonance", 88, true, sink, registry),
          mix_(apvts, ids::mix, "Mix", 88, false, sink, registry) {
        addAndMakeVisible(scan_);
        addAndMakeVisible(cutoff_);
        addAndMakeVisible(resonance_);
        addAndMakeVisible(mix_);
    }

    void paint(juce::Graphics& g) override {
        theme::paintPanel(g, getLocalBounds().toFloat());
    }

    void resized() override {
        auto r = getLocalBounds().reduced(10, 6);
        const int cellW = r.getWidth() / 4;
        scan_.setBounds(r.removeFromLeft(cellW));
        cutoff_.setBounds(r.removeFromLeft(cellW));
        resonance_.setBounds(r.removeFromLeft(cellW));
        mix_.setBounds(r);
    }

private:
    LabeledKnob scan_, cutoff_, resonance_, mix_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KnobPanel)
};

} // namespace ftus
