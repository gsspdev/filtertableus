// FilterTableUS GUI — editor implementation + the frozen createFtusEditor seam.
// Replaces the Phase 0 GenericAudioProcessorEditor stub.
#include "gui/PluginEditor.h"

#include <iostream>

#include "ftus/EditorFactory.h"

namespace ftus {

namespace {
/// GUI scale is persisted as a root property on the APVTS state tree so it rides along with
/// session state even before the real StateManager lands. TODO(integration): move to the
/// dedicated <GUI> node once StateManagerImpl defines the final schema (plan §3).
constexpr const char* kGuiScaleProperty = "guiScale";
} // namespace

FtusEditor::FtusEditor(FtusAudioProcessor& processor)
    : juce::AudioProcessorEditor(processor), processor_(processor),
      content_(processor, bindings_) {
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(content_);
    processor_.addChangeListener(this);

    using namespace theme::layout;
    setResizable(true, true);
    setResizeLimits(juce::roundToInt(kWidth * kMinScale), juce::roundToInt(kHeight * kMinScale),
                    juce::roundToInt(kWidth * kMaxScale), juce::roundToInt(kHeight * kMaxScale));
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio(kAspect);

    const double persisted = static_cast<double>(
        processor_.state().state.getProperty(kGuiScaleProperty, 1.0));
    const double scale = juce::jlimit(kMinScale, kMaxScale, persisted);
    setSize(juce::roundToInt(kWidth * scale), juce::roundToInt(kHeight * scale));
    scaleRestoreDone_ = true;

    // Acceptance check: every one of the 27 parameters must be bound via an attachment.
    const auto missing = bindings_.missingIn(processor_);
    std::cerr << "FTUS-GUI bindings: " << bindings_.size() << " parameter IDs bound, "
              << missing.size() << " missing";
    if (!missing.isEmpty())
        std::cerr << " [" << missing.joinIntoString(", ") << "]";
    std::cerr << std::endl;
    jassert(missing.isEmpty());
}

FtusEditor::~FtusEditor() {
    processor_.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void FtusEditor::resized() {
    using namespace theme::layout;
    const float scale = static_cast<float>(getWidth()) / static_cast<float>(kWidth);
    content_.setTransform(juce::AffineTransform::scale(scale));
    content_.setBounds(0, 0, kWidth, kHeight);
    if (scaleRestoreDone_) // persist (message thread; skip the initial restore itself)
        processor_.state().state.setProperty(kGuiScaleProperty, static_cast<double>(scale),
                                             nullptr);
}

void FtusEditor::changeListenerCallback(juce::ChangeBroadcaster*) {
    // Table adopted / load failed / preset changed — refresh the info-bearing strips.
    content_.wavetableRow.refresh();
    content_.presetBar.refresh();
}

bool FtusEditor::isInterestedInFileDrag(const juce::StringArray& files) {
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".wav"))
            return true;
    return false;
}

void FtusEditor::fileDragEnter(const juce::StringArray&, int, int) {
    dragHover_ = true;
    repaint();
}

void FtusEditor::fileDragExit(const juce::StringArray&) {
    dragHover_ = false;
    repaint();
}

void FtusEditor::filesDropped(const juce::StringArray& files, int, int) {
    dragHover_ = false;
    repaint();
    for (const auto& f : files) {
        if (f.endsWithIgnoreCase(".wav")) {
            processor_.loader().requestLoadFile(juce::File(f)); // message thread by contract
            break;
        }
    }
}

void FtusEditor::paintOverChildren(juce::Graphics& g) {
    if (!dragHover_)
        return;
    auto r = getLocalBounds().toFloat();
    g.setColour(theme::bg.withAlpha(0.62f));
    g.fillRect(r);
    g.setColour(theme::accent);
    const float dashes[] = {7.0f, 5.0f};
    juce::Path border;
    border.addRoundedRectangle(r.reduced(10.0f), 8.0f);
    juce::PathStrokeType(2.0f).createDashedStroke(border, border, dashes, 2);
    g.fillPath(border);
    g.setFont(FtusLookAndFeel::font(19.0f, true));
    g.drawText("Drop .wav to load wavetable", r, juce::Justification::centred, false);
}

// Frozen seam (ftus/EditorFactory.h): the shell calls this from createEditor().
juce::AudioProcessorEditor* createFtusEditor(FtusAudioProcessor& processor) {
    return new FtusEditor(processor);
}

} // namespace ftus
