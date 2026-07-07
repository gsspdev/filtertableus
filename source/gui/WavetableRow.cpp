#include "gui/WavetableRow.h"

#include "gui/FtusLookAndFeel.h"
#include "ftus/FactoryTables.h"

namespace ftus {

WavetableRow::WavetableRow(FtusAudioProcessor& processor) : processor_(processor) {
    loadButton_.onClick = [this] { openFileChooser(); };
    loadButton_.setTooltip("Load a .wav wavetable or sample");
    addAndMakeVisible(loadButton_);

    factoryButton_.onClick = [this] { showFactoryMenu(); };
    factoryButton_.setTooltip("Built-in factory wavetables");
    addAndMakeVisible(factoryButton_);

    refresh();
    startTimer(100); // progress polling + toast timeout
}

void WavetableRow::refresh() {
    auto table = processor_.engine().currentTableForUi();
    const auto& info = processor_.currentTableInfo();
    nameText_ = info.displayName.isNotEmpty()
                    ? info.displayName
                    : (table != nullptr ? juce::String(table->name()) : juce::String("No wavetable"));
    framesText_ = table != nullptr
                      ? juce::String(table->numFrames())
                            + (table->numFrames() == 1 ? " frame" : " frames")
                      : juce::String("load a .wav or pick a factory table");

    const auto& err = processor_.lastLoadError();
    if (err.isNotEmpty() && (err != lastError_ || !toastVisible_))
        showToast(err);
    lastError_ = err;
    repaint();
}

void WavetableRow::showToast(const juce::String& message) {
    toastText_ = message;
    toastVisible_ = true;
    toastEndMs_ = juce::Time::getMillisecondCounter() + 4000; // 4 s per plan §4
    repaint();
}

void WavetableRow::timerCallback() {
    const float p = processor_.loader().progress();
    if (std::abs(p - progress_) > 0.004f) {
        progress_ = p;
        repaint();
    }
    if (toastVisible_ && juce::Time::getMillisecondCounter() >= toastEndMs_) {
        toastVisible_ = false;
        repaint();
    }
}

void WavetableRow::openFileChooser() {
    chooser_ = std::make_unique<juce::FileChooser>(
        "Load wavetable (.wav)",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory), "*.wav");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
                          [this](const juce::FileChooser& fc) {
                              const auto file = fc.getResult();
                              if (file.existsAsFile())
                                  processor_.loader().requestLoadFile(file);
                          });
}

void WavetableRow::showFactoryMenu() {
    juce::PopupMenu menu;
    for (int i = 0; i < kNumFactoryTables; ++i)
        menu.addItem(i + 1, factoryTableDisplayName(static_cast<FactoryTableId>(i)));
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&factoryButton_),
                       [this](int result) {
                           if (result >= 1 && result <= kNumFactoryTables)
                               processor_.loader().requestFactoryTable(
                                   static_cast<FactoryTableId>(result - 1));
                       });
}

void WavetableRow::resized() {
    const int cy = (getHeight() - 22) / 2;
    factoryButton_.setBounds(getWidth() - 12 - 82, cy, 82, 22);
    loadButton_.setBounds(getWidth() - 12 - 82 - 8 - 74, cy, 74, 22);
}

void WavetableRow::paint(juce::Graphics& g) {
    auto area = getLocalBounds();

    // Name (medium) + frame count (dim).
    const juce::Font nameFont = FtusLookAndFeel::font(13.0f, true);
    g.setFont(nameFont);
    g.setColour(theme::text);
    const auto textArea = area.toFloat().withTrimmedLeft(14.0f).withTrimmedRight(200.0f);
    g.drawText(nameText_, textArea, juce::Justification::centredLeft, true);
    const float nameW = juce::GlyphArrangement::getStringWidth(nameFont, nameText_);
    g.setFont(FtusLookAndFeel::font(11.5f));
    g.setColour(theme::textDim);
    g.drawText(framesText_, textArea.withTrimmedLeft(nameW + 12.0f),
               juce::Justification::centredLeft, true);

    // Drop hint, right-aligned before the buttons.
    g.setFont(FtusLookAndFeel::font(10.0f));
    g.setColour(theme::textDim.withAlpha(0.65f));
    g.drawText("drop .wav anywhere",
               juce::Rectangle<float>(static_cast<float>(loadButton_.getX() - 158), 0.0f, 150.0f,
                                      static_cast<float>(getHeight())),
               juce::Justification::centredRight, false);

    // Bottom hairline.
    g.setColour(theme::stroke);
    g.fillRect(0.0f, static_cast<float>(getHeight()) - 1.0f, static_cast<float>(getWidth()),
               1.0f);

    // Thin loader progress bar along the bottom edge.
    if (progress_ > 0.0f && progress_ < 1.0f) {
        g.setColour(theme::accent);
        g.fillRect(0.0f, static_cast<float>(getHeight()) - 3.0f,
                   static_cast<float>(getWidth()) * progress_, 2.0f);
    }
}

void WavetableRow::paintOverChildren(juce::Graphics& g) {
    if (!toastVisible_)
        return;
    auto r = getLocalBounds().toFloat().reduced(160.0f, 3.5f);
    g.setColour(theme::warning.withAlpha(0.96f));
    g.fillRoundedRectangle(r, 5.0f);
    g.setColour(juce::Colours::white);
    g.setFont(FtusLookAndFeel::font(12.0f, true));
    g.drawText(toastText_, r.reduced(10.0f, 0.0f), juce::Justification::centred, true);
}

} // namespace ftus
