// FilterTableUS GUI — the editor behind the frozen ftus/EditorFactory.h seam.
// 920x620 design space, resizable 0.75x..1.75x at a fixed aspect ratio: children are laid out
// once in design coordinates on a content component, and the editor applies a uniform
// AffineTransform scale in resized(). The whole surface is a FileDragAndDropTarget for .wav.
#pragma once
#include "gui/FtusLookAndFeel.h"
#include "gui/KnobPanel.h"
#include "gui/ModPanel.h"
#include "gui/PhaseModeSelector.h"
#include "gui/PresetBar.h"
#include "gui/SmallKnobRow.h"
#include "gui/SpectrumView.h"
#include "gui/Theme.h"
#include "gui/ValueReadout.h"
#include "gui/WaterfallView.h"
#include "gui/WavetableRow.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

/// Fixed-size (920x620) container holding every panel; the editor scales it as one unit.
class FtusEditorContent : public juce::Component {
public:
    FtusEditorContent(FtusAudioProcessor& processor, BindingRegistry& registry)
        : presetBar(processor, registry),
          wavetableRow(processor),
          waterfall(processor, registry, &readout),
          spectrum(processor),
          phaseMode(processor, registry, &readout),
          smallKnobs(processor.state(), registry, &readout),
          knobPanel(processor.state(), registry, &readout),
          modPanel(processor, registry, &readout) {
        addAndMakeVisible(presetBar);
        addAndMakeVisible(wavetableRow);
        addAndMakeVisible(waterfall);
        addAndMakeVisible(spectrum);
        addAndMakeVisible(phaseMode);
        addAndMakeVisible(smallKnobs);
        addAndMakeVisible(readout);
        addAndMakeVisible(knobPanel);
        addAndMakeVisible(modPanel);
        setSize(theme::layout::kWidth, theme::layout::kHeight);
    }

    void paint(juce::Graphics& g) override { g.fillAll(theme::bg); }

    void resized() override {
        presetBar.setBounds(theme::layout::presetBar());
        wavetableRow.setBounds(theme::layout::wavetableRow());
        waterfall.setBounds(theme::layout::waterfall());
        spectrum.setBounds(theme::layout::spectrum());
        phaseMode.setBounds(theme::layout::phaseMode());
        smallKnobs.setBounds(theme::layout::smallKnobs());
        readout.setBounds(theme::layout::readout());
        knobPanel.setBounds(theme::layout::knobPanel());
        modPanel.setBounds(theme::layout::modPanel());
    }

    ValueReadout readout; // declared first: every panel receives it as the ReadoutSink
    PresetBar presetBar;
    WavetableRow wavetableRow;
    WaterfallView waterfall;
    SpectrumView spectrum;
    PhaseModeSelector phaseMode;
    SmallKnobRow smallKnobs;
    KnobPanel knobPanel;
    ModPanel modPanel;
};

class FtusEditor : public juce::AudioProcessorEditor,
                   public juce::FileDragAndDropTarget,
                   private juce::ChangeListener {
public:
    explicit FtusEditor(FtusAudioProcessor& processor);
    ~FtusEditor() override;

    void paint(juce::Graphics& g) override { g.fillAll(theme::bg); }
    void paintOverChildren(juce::Graphics&) override; // drag-and-drop highlight overlay
    void resized() override;

    // FileDragAndDropTarget (whole editor surface, .wav only)
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray&, int, int) override;
    void fileDragExit(const juce::StringArray&) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

    const BindingRegistry& bindings() const noexcept { return bindings_; }

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;

    FtusAudioProcessor& processor_;
    FtusLookAndFeel lookAndFeel_;
    BindingRegistry bindings_;
    FtusEditorContent content_;
    juce::TooltipWindow tooltipWindow_{this, 600};
    bool dragHover_ = false;
    bool scaleRestoreDone_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FtusEditor)
};

} // namespace ftus
