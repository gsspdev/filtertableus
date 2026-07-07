// FilterTableUS GUI — flat segment button (phase-mode segments, mod-panel tabs).
#pragma once
#include "gui/FtusLookAndFeel.h"
#include "gui/Theme.h"

namespace ftus {

class SegmentButton : public juce::Component, public juce::SettableTooltipClient {
public:
    /// verticalMarker=false: accent underline (horizontal segment rows).
    /// verticalMarker=true: accent bar on the left edge (vertical tab stacks).
    explicit SegmentButton(juce::String text, bool verticalMarker = false)
        : text_(std::move(text)), vertical_(verticalMarker) {
        setRepaintsOnMouseActivity(true);
    }

    std::function<void()> onClick;
    std::function<void(bool)> onHoverChanged;

    void setActive(bool shouldBeActive) {
        if (active_ != shouldBeActive) {
            active_ = shouldBeActive;
            repaint();
        }
    }
    bool isActive() const noexcept { return active_; }
    const juce::String& text() const noexcept { return text_; }

    void paint(juce::Graphics& g) override {
        auto r = getLocalBounds().toFloat();
        if (isMouseOver())
            g.setColour(juce::Colours::white.withAlpha(0.04f)),
                g.fillRoundedRectangle(r.reduced(1.0f), 3.0f);

        g.setColour(active_ ? theme::text : theme::textDim);
        g.setFont(FtusLookAndFeel::font(12.0f, active_));
        g.drawText(text_, r.reduced(6.0f, 2.0f), juce::Justification::centred, true);

        if (active_) {
            g.setColour(theme::accent);
            if (vertical_)
                g.fillRoundedRectangle(r.getX() + 1.0f, r.getY() + 3.0f, 2.5f,
                                       r.getHeight() - 6.0f, 1.25f);
            else
                g.fillRoundedRectangle(r.getCentreX() - r.getWidth() * 0.28f, r.getBottom() - 3.0f,
                                       r.getWidth() * 0.56f, 2.0f, 1.0f);
        }
    }

    void mouseEnter(const juce::MouseEvent&) override {
        if (onHoverChanged)
            onHoverChanged(true);
    }
    void mouseExit(const juce::MouseEvent&) override {
        if (onHoverChanged)
            onHoverChanged(false);
    }
    void mouseUp(const juce::MouseEvent& e) override {
        if (getLocalBounds().contains(e.getPosition()) && onClick)
            onClick();
    }

private:
    juce::String text_;
    bool active_ = false;
    bool vertical_ = false;
};

} // namespace ftus
