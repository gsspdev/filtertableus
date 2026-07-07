#include "gui/SpectrumView.h"

#include <cmath>

#include "gui/FtusLookAndFeel.h"

namespace ftus {

SpectrumView::SpectrumView(FtusAudioProcessor& processor) : processor_(processor) {
    setInterceptsMouseClicks(false, false);
    startTimerHz(30);
}

void SpectrumView::timerCallback() {
    if (processor_.engine().readResponseCurve(curve_)) {
        hasCurve_ = true;
        rebuildPaths();
        repaint(); // gated: only fresh TripleBuffer data triggers a repaint
    }
}

void SpectrumView::resized() {
    if (hasCurve_)
        rebuildPaths();
}

juce::Rectangle<float> SpectrumView::plotArea() const {
    return getLocalBounds().toFloat().reduced(8.0f, 6.0f).withTrimmedBottom(12.0f);
}

float SpectrumView::xForFrequency(float hz, juce::Rectangle<float> r) const {
    const float t = std::log(hz / ftc::ResponseCurve::kMinHz)
                    / std::log(ftc::ResponseCurve::kMaxHz / ftc::ResponseCurve::kMinHz);
    return r.getX() + r.getWidth() * juce::jlimit(0.0f, 1.0f, t);
}

float SpectrumView::yForDb(float db, juce::Rectangle<float> r) const {
    const float clamped = juce::jlimit(kMinDb, kMaxDb, db);
    return r.getBottom() - (clamped - kMinDb) / (kMaxDb - kMinDb) * r.getHeight();
}

void SpectrumView::rebuildPaths() {
    strokePath_.clear();
    fillPath_.clear();
    const auto r = plotArea();
    if (r.isEmpty())
        return;

    strokePath_.preallocateSpace(3 * ftc::ResponseCurve::kNumPoints + 12);
    for (int i = 0; i < ftc::ResponseCurve::kNumPoints; ++i) {
        // The ONLY frequency grid: ftc::ResponseCurve::frequencyForPoint (docs/INTERFACES.md).
        const float x = xForFrequency(ftc::ResponseCurve::frequencyForPoint(i), r);
        const float y = yForDb(curve_.db[static_cast<size_t>(i)], r);
        if (i == 0)
            strokePath_.startNewSubPath(x, y);
        else
            strokePath_.lineTo(x, y);
    }
    fillPath_ = strokePath_;
    fillPath_.lineTo(r.getRight(), r.getBottom());
    fillPath_.lineTo(r.getX(), r.getBottom());
    fillPath_.closeSubPath();
}

void SpectrumView::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());
    const auto r = plotArea();

    // Octave gridlines (faint) + decade lines (brighter, labelled).
    for (float hz = 40.0f; hz < 20000.0f; hz *= 2.0f) {
        const float x = xForFrequency(hz, r);
        g.setColour(theme::strokeSoft);
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
    }
    g.setFont(FtusLookAndFeel::font(9.0f));
    for (const float hz : {100.0f, 1000.0f, 10000.0f}) {
        const float x = xForFrequency(hz, r);
        g.setColour(theme::stroke);
        g.drawVerticalLine(juce::roundToInt(x), r.getY(), r.getBottom());
        g.setColour(theme::textDim);
        const juce::String label = hz >= 1000.0f ? juce::String(hz / 1000.0f, 0) + "k"
                                                 : juce::String(juce::roundToInt(hz));
        g.drawText(label, juce::Rectangle<float>(x - 16.0f, r.getBottom() + 1.0f, 32.0f, 10.0f),
                   juce::Justification::centred, false);
    }

    // dB gridlines every 10 dB; 0 dB emphasized; -30/0/+30 labelled.
    for (float db = kMinDb; db <= kMaxDb; db += 10.0f) {
        const float y = yForDb(db, r);
        g.setColour(std::abs(db) < 0.01f ? theme::stroke.brighter(0.25f) : theme::strokeSoft);
        g.drawHorizontalLine(juce::roundToInt(y), r.getX(), r.getRight());
    }
    g.setColour(theme::textDim);
    for (const float db : {kMaxDb, 0.0f, kMinDb}) {
        const float y = yForDb(db, r);
        // Keep the +30 label below its line and the -30 label above it (avoid clipping).
        const float labelY = db > 0.0f ? y + 2.0f : y - 11.0f;
        const juce::String label = (db > 0 ? "+" : "") + juce::String(juce::roundToInt(db));
        g.drawText(label, juce::Rectangle<float>(r.getX() + 2.0f, labelY, 24.0f, 9.0f),
                   juce::Justification::centredLeft, false);
    }

    // Response curve: translucent accent fill + 1.5 px stroke.
    if (hasCurve_) {
        g.setColour(theme::accent.withAlpha(0.16f));
        g.fillPath(fillPath_);
        g.setColour(theme::accent);
        g.strokePath(strokePath_, juce::PathStrokeType(1.5f, juce::PathStrokeType::curved));
    } else {
        g.setColour(theme::textDim.withAlpha(0.6f));
        g.setFont(FtusLookAndFeel::font(10.5f));
        g.drawText("response", r, juce::Justification::centred, false);
    }
}

} // namespace ftus
