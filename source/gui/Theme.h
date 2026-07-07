// FilterTableUS GUI — palette, design-space layout, small shared helpers.
// Owned by the GUI workstream (source/gui/**).
#pragma once
#include <set>

#include <juce_audio_processors/juce_audio_processors.h>

namespace ftus::theme {

// Palette (plan §5).
inline const juce::Colour bg{0xFF141619};          // window background
inline const juce::Colour panel{0xFF1C1F26};       // panel fill
inline const juce::Colour panelRaised{0xFF20242C}; // menus, tooltips, toasts
inline const juce::Colour control{0xFF232833};     // knob bodies, buttons, combos
inline const juce::Colour stroke{0xFF2A2E37};      // outlines / gridlines
inline const juce::Colour strokeSoft{0xFF20242B};  // faint gridlines
inline const juce::Colour text{0xFFC9CED6};
inline const juce::Colour textDim{0xFF7A828E};
inline const juce::Colour accent{0xFFFF8A3D};
inline const juce::Colour warning{0xFFE5484D};
inline const juce::Colour wave{0xFF8F98A6};        // waterfall polylines

namespace layout {
// The whole editor is laid out in a fixed 920x620 design space; the editor applies a
// uniform AffineTransform scale (0.75..1.75) on top (plan §5).
inline constexpr int kWidth = 920;
inline constexpr int kHeight = 620;
inline constexpr double kAspect = static_cast<double>(kWidth) / static_cast<double>(kHeight);
inline constexpr double kMinScale = 0.75;
inline constexpr double kMaxScale = 1.75;

inline juce::Rectangle<int> presetBar() { return {0, 0, 920, 36}; }
inline juce::Rectangle<int> wavetableRow() { return {0, 36, 920, 34}; }
inline juce::Rectangle<int> waterfall() { return {12, 78, 588, 332}; }
inline juce::Rectangle<int> spectrum() { return {612, 78, 296, 170}; }
inline juce::Rectangle<int> phaseMode() { return {612, 256, 296, 40}; }
inline juce::Rectangle<int> smallKnobs() { return {612, 304, 296, 70}; }
inline juce::Rectangle<int> readout() { return {612, 382, 296, 28}; }
inline juce::Rectangle<int> knobPanel() { return {12, 418, 588, 120}; }
inline juce::Rectangle<int> modPanel() { return {0, 546, 920, 74}; }
} // namespace layout

/// Standard panel background + outline.
inline void paintPanel(juce::Graphics& g, juce::Rectangle<float> r, float corner = 6.0f) {
    g.setColour(panel);
    g.fillRoundedRectangle(r, corner);
    g.setColour(stroke);
    g.drawRoundedRectangle(r.reduced(0.5f), corner, 1.0f);
}

} // namespace ftus::theme

namespace ftus {

/// Records which parameter IDs the editor bound via attachments; the editor asserts full
/// coverage of the 27-parameter set at construction (acceptance criterion 2).
class BindingRegistry {
public:
    void add(const juce::String& paramId) { bound_.insert(paramId); }

    juce::StringArray missingIn(const juce::AudioProcessor& proc) const {
        juce::StringArray missing;
        for (auto* p : proc.getParameters())
            if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(p))
                if (bound_.count(withId->paramID) == 0)
                    missing.add(withId->paramID);
        return missing;
    }

    int size() const { return static_cast<int>(bound_.size()); }

private:
    std::set<juce::String> bound_;
};

/// Shared hover/drag readout strip target (implemented by ValueReadout).
struct ReadoutSink {
    virtual ~ReadoutSink() = default;
    virtual void showReadout(const juce::String& name, const juce::String& value) = 0;
    virtual void clearReadout() = 0;
};

/// Human formatting for the readout strip, derived from the frozen parameter layout
/// (labels "Hz"/"dB"/"ms" set in source/plugin/Parameters.cpp).
inline juce::String formatParameterValue(const juce::RangedAudioParameter& p) {
    if (dynamic_cast<const juce::AudioParameterChoice*>(&p) != nullptr
        || dynamic_cast<const juce::AudioParameterBool*>(&p) != nullptr)
        return p.getCurrentValueAsText();

    const float v = p.convertFrom0to1(p.getValue());
    const auto label = p.getLabel();
    if (label == "Hz")
        return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " kHz"
                            : juce::String(v, v < 100.0f ? 2 : 1) + " Hz";
    if (label == "dB")
        return juce::String(v, 1) + " dB";
    if (label == "ms")
        return v >= 1000.0f ? juce::String(v / 1000.0f, 2) + " s" : juce::String(v, 1) + " ms";

    const auto& range = p.getNormalisableRange();
    if (juce::approximatelyEqual(range.start, -1.0f) && juce::approximatelyEqual(range.end, 1.0f)) {
        const int pc = juce::roundToInt(v * 100.0f);
        return (pc > 0 ? "+" : "") + juce::String(pc) + " %";
    }
    if (juce::approximatelyEqual(range.start, 0.0f) && juce::approximatelyEqual(range.end, 1.0f))
        return juce::String(juce::roundToInt(v * 100.0f)) + " %";
    return p.getCurrentValueAsText();
}

} // namespace ftus
