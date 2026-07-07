// FilterTableUS GUI — LookAndFeel: Inter typography, 270-degree arc rotaries with a
// center-notched bipolar variant, flat dark widgets. Owned by the GUI workstream.
#pragma once
#include "gui/Theme.h"

namespace ftus {

class FtusLookAndFeel : public juce::LookAndFeel_V4 {
public:
    FtusLookAndFeel();

    /// Inter font from the ftus_assets binary data; falls back to the system sans-serif
    /// if the embedded typeface fails to load.
    static juce::Font font(float height, bool medium = false);
    static juce::Typeface::Ptr regularTypeface();
    static juce::Typeface::Ptr mediumTypeface();

    // Rotaries: 270-degree arc, track + value arc + pointer. Sliders with the property
    // "ftusBipolar" fill from 12 o'clock and get a center notch.
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;

    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                              bool shouldDrawButtonAsHighlighted,
                              bool shouldDrawButtonAsDown) override;
    void drawToggleButton(juce::Graphics&, juce::ToggleButton&, bool shouldDrawButtonAsHighlighted,
                          bool shouldDrawButtonAsDown) override;

    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getAlertWindowTitleFont() override;
    juce::Font getAlertWindowMessageFont() override;
    juce::Font getAlertWindowFont() override;

    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText,
                                          juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;
};

} // namespace ftus
