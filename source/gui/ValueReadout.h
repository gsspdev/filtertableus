// FilterTableUS GUI — shared readout strip: shows "parameter name : formatted value" while a
// control is hovered or dragged (plan §5).
#pragma once
#include "gui/FtusLookAndFeel.h"
#include "gui/Theme.h"

namespace ftus {

class ValueReadout : public juce::Component, public ReadoutSink {
public:
    ValueReadout() { setInterceptsMouseClicks(false, false); }

    void showReadout(const juce::String& name, const juce::String& value) override {
        if (name_ != name || value_ != value) {
            name_ = name;
            value_ = value;
            repaint();
        }
    }

    void clearReadout() override {
        if (name_.isNotEmpty() || value_.isNotEmpty()) {
            name_.clear();
            value_.clear();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        g.setColour(theme::panel);
        g.fillRoundedRectangle(r, 5.0f);
        g.setColour(theme::stroke);
        g.drawRoundedRectangle(r.reduced(0.5f), 5.0f, 1.0f);

        auto inner = getLocalBounds().reduced(10, 2);
        if (name_.isEmpty() && value_.isEmpty()) {
            g.setColour(theme::textDim.withAlpha(0.7f));
            g.setFont(FtusLookAndFeel::font(10.5f));
            g.drawText("hover a control", inner, juce::Justification::centred, true);
            return;
        }
        g.setColour(theme::textDim);
        g.setFont(FtusLookAndFeel::font(11.0f));
        g.drawText(name_, inner, juce::Justification::centredLeft, true);
        g.setColour(theme::text);
        g.setFont(FtusLookAndFeel::font(12.0f, true));
        g.drawText(value_, inner, juce::Justification::centredRight, true);
    }

private:
    juce::String name_, value_;
};

} // namespace ftus
