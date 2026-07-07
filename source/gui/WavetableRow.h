// FilterTableUS GUI — wavetable strip: current table name + frame count, Load… (async file
// chooser -> LoaderService::requestLoadFile), factory-table popup (-> requestFactoryTable),
// thin progress bar polling LoaderService::progress(), and a 4 s error toast on failed loads.
#pragma once
#include "gui/Theme.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

class WavetableRow : public juce::Component, private juce::Timer {
public:
    explicit WavetableRow(FtusAudioProcessor& processor);

    /// Re-reads table info + last load error (editor calls this on processor change events).
    void refresh();

    void paint(juce::Graphics&) override;
    void paintOverChildren(juce::Graphics&) override; // toast overlays the buttons
    void resized() override;

private:
    void timerCallback() override;
    void showToast(const juce::String& message);
    void openFileChooser();
    void showFactoryMenu();

    FtusAudioProcessor& processor_;

    juce::TextButton loadButton_{"Load..."};
    juce::TextButton factoryButton_{"Factory"};
    std::unique_ptr<juce::FileChooser> chooser_;

    juce::String nameText_{"No wavetable"};
    juce::String framesText_;

    float progress_ = 0.0f;

    juce::String toastText_;
    bool toastVisible_ = false;
    juce::uint32 toastEndMs_ = 0;
    juce::String lastError_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableRow)
};

} // namespace ftus
