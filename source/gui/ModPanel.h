// FilterTableUS GUI — modulation panel: tabs LFO1 | LFO2 | ENV (plan §5).
// LFO tab: Rate knob <-> Division combo swapped by the Sync toggle, Shape combo, Retrig
// toggle, two bipolar depth knobs. ENV tab: Sens/Attack/Release + two depth knobs + a live
// envelope meter polling engine().envValue() on a timer.
#pragma once
#include "gui/LabeledKnob.h"
#include "gui/SegmentButton.h"
#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

/// One LFO page; `which` selects the lfo1*/lfo2* parameter-ID set.
class LfoPage : public juce::Component {
public:
    LfoPage(juce::AudioProcessorValueTreeState& apvts, int which, BindingRegistry& registry,
            ReadoutSink* sink);

    void resized() override;

private:
    void updateRateDivVisibility();

    LabeledKnob rate_;
    juce::ComboBox divBox_, shapeBox_;
    juce::ToggleButton syncButton_{"SYNC"}, retrigButton_{"RETRIG"};
    LabeledKnob toScan_, toCutoff_;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> divAttachment_,
        shapeAttachment_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> syncAttachment_,
        retrigAttachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LfoPage)
};

/// Live envelope-follower meter — polls engine().envValue() (relaxed atomic) at 30 Hz and
/// repaints only when the value moved visibly.
class EnvMeter : public juce::Component, private juce::Timer {
public:
    explicit EnvMeter(FtusAudioProcessor& processor) : processor_(processor) {
        setInterceptsMouseClicks(false, false);
        startTimerHz(30);
    }

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override {
        const float v = juce::jlimit(0.0f, 1.0f, processor_.engine().envValue());
        if (std::abs(v - value_) > 0.004f) {
            value_ = v;
            repaint();
        }
    }

    FtusAudioProcessor& processor_;
    float value_ = 0.0f;
};

class EnvPage : public juce::Component {
public:
    EnvPage(FtusAudioProcessor& processor, BindingRegistry& registry, ReadoutSink* sink);

    void resized() override;

private:
    LabeledKnob sens_, attack_, release_, toScan_, toCutoff_;
    EnvMeter meter_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvPage)
};

class ModPanel : public juce::Component {
public:
    ModPanel(FtusAudioProcessor& processor, BindingRegistry& registry, ReadoutSink* sink);

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void setActiveTab(int index);

    juce::OwnedArray<SegmentButton> tabs_;
    LfoPage lfo1_, lfo2_;
    EnvPage env_;
    int activeTab_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModPanel)
};

} // namespace ftus
