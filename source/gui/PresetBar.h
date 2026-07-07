// FilterTableUS GUI — top bar: logo, prev/next preset arrows, preset-name popup, Save,
// dirty '*', hosted-bypass toggle. Drives everything through the StateManager interface only
// (the Phase 0 stub returns empty lists — handled gracefully).
#pragma once
#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

class PresetBar : public juce::Component, private juce::Timer {
public:
    PresetBar(FtusAudioProcessor& processor, BindingRegistry& registry);

    /// Re-reads preset name / dirty flag right now (editor calls this on processor changes).
    void refresh();

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override { refresh(); }
    void showPresetMenu();
    void promptSaveName();

    FtusAudioProcessor& processor_;

    juce::ArrowButton prevButton_{"prevPreset", 0.5f, theme::textDim};
    juce::ArrowButton nextButton_{"nextPreset", 0.0f, theme::textDim};
    juce::TextButton nameButton_;
    juce::TextButton saveButton_{"Save"};
    juce::TextButton bypassButton_{"Bypass"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment_;

    juce::String shownName_;
    bool shownDirty_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBar)
};

} // namespace ftus
