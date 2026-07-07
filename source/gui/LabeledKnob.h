// FilterTableUS GUI — rotary slider + caption, bound to one APVTS parameter via
// SliderAttachment. Feeds the shared ValueReadout on hover/drag.
#pragma once
#include "gui/FtusLookAndFeel.h"
#include "gui/Theme.h"

namespace ftus {

class LabeledKnob : public juce::Component {
public:
    LabeledKnob(juce::AudioProcessorValueTreeState& apvts, const char* paramId,
                const juce::String& caption, int knobDiameter, bool bipolar, ReadoutSink* sink,
                BindingRegistry& registry)
        : knobDiameter_(knobDiameter), sink_(sink) {
        param_ = apvts.getParameter(paramId);
        jassert(param_ != nullptr);

        slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider_.setRotaryParameters(juce::MathConstants<float>::pi * 1.25f,
                                    juce::MathConstants<float>::pi * 2.75f, true);
        if (bipolar)
            slider_.getProperties().set("ftusBipolar", true);
        slider_.onValueChange = [this] {
            if (sink_ != nullptr && (isMouseOverOrDragging(true) || slider_.isMouseButtonDown()))
                pushReadout();
        };
        addAndMakeVisible(slider_);

        label_.setText(caption, juce::dontSendNotification);
        label_.setJustificationType(juce::Justification::centred);
        label_.setFont(FtusLookAndFeel::font(11.0f));
        label_.setColour(juce::Label::textColourId, theme::textDim);
        label_.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(label_);

        attachment_ = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, slider_);
        registry.add(paramId);

        addMouseListener(this, true); // hover events from the child slider too
    }

    ~LabeledKnob() override { removeMouseListener(this); }

    void resized() override {
        auto b = getLocalBounds();
        label_.setBounds(b.removeFromBottom(14));
        const int d = juce::jmin(knobDiameter_, b.getWidth(), b.getHeight());
        slider_.setBounds(b.withSizeKeepingCentre(d, d));
    }

    void mouseEnter(const juce::MouseEvent&) override { pushReadout(); }
    void mouseMove(const juce::MouseEvent&) override { pushReadout(); }
    void mouseExit(const juce::MouseEvent&) override {
        if (sink_ != nullptr && !isMouseOverOrDragging(true))
            sink_->clearReadout();
    }

    juce::Slider& slider() noexcept { return slider_; }

private:
    void pushReadout() {
        if (sink_ != nullptr && param_ != nullptr)
            sink_->showReadout(param_->getName(64), formatParameterValue(*param_));
    }

    juce::Slider slider_;
    juce::Label label_;
    juce::RangedAudioParameter* param_ = nullptr;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment_;
    int knobDiameter_;
    ReadoutSink* sink_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LabeledKnob)
};

} // namespace ftus
