#include "gui/PresetBar.h"

#include "gui/FtusLookAndFeel.h"
#include "ftus/PluginIDs.h"

namespace ftus {

PresetBar::PresetBar(FtusAudioProcessor& processor, BindingRegistry& registry)
    : processor_(processor) {
    prevButton_.onClick = [this] {
        processor_.stateManager().prevPreset();
        refresh();
    };
    nextButton_.onClick = [this] {
        processor_.stateManager().nextPreset();
        refresh();
    };
    addAndMakeVisible(prevButton_);
    addAndMakeVisible(nextButton_);

    nameButton_.setButtonText("Init");
    nameButton_.onClick = [this] { showPresetMenu(); };
    nameButton_.setTooltip("Preset browser");
    addAndMakeVisible(nameButton_);

    saveButton_.onClick = [this] { promptSaveName(); };
    saveButton_.setTooltip("Save as user preset");
    addAndMakeVisible(saveButton_);

    bypassButton_.setClickingTogglesState(true);
    bypassButton_.setColour(juce::TextButton::buttonOnColourId, theme::warning);
    bypassButton_.setColour(juce::TextButton::textColourOnId, juce::Colours::white);
    bypassButton_.setTooltip("Hosted bypass");
    addAndMakeVisible(bypassButton_);
    bypassAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        processor_.state(), ids::bypass, bypassButton_);
    registry.add(ids::bypass);

    refresh();
    startTimer(300); // poll dirty flag / external preset changes
}

void PresetBar::refresh() {
    const auto name = processor_.stateManager().currentPresetName();
    const bool dirty = processor_.stateManager().isDirty();
    if (name != shownName_ || dirty != shownDirty_) {
        shownName_ = name;
        shownDirty_ = dirty;
        nameButton_.setButtonText(shownName_.isNotEmpty() ? shownName_ + (dirty ? " *" : "")
                                                          : juce::String("Init"));
    }
}

void PresetBar::showPresetMenu() {
    juce::PopupMenu menu;
    const auto names = processor_.stateManager().listPresets(); // factory first, then user
    if (names.isEmpty()) {
        menu.addItem(1, "No presets installed", false);
    } else {
        for (int i = 0; i < names.size(); ++i)
            menu.addItem(i + 2, names[i], true, names[i] == shownName_);
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&nameButton_),
                       [this, names](int result) {
                           if (result >= 2 && result - 2 < names.size()) {
                               processor_.stateManager().loadPreset(names[result - 2]);
                               refresh();
                           }
                       });
}

void PresetBar::promptSaveName() {
    auto* w = new juce::AlertWindow("Save Preset", "Preset name:",
                                    juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor("name", processor_.stateManager().currentPresetName(), {});
    w->addButton("Save", 1, juce::KeyPress(juce::KeyPress::returnKey));
    w->addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
    w->enterModalState(true, juce::ModalCallbackFunction::create([this, w](int result) {
                           if (result == 1) {
                               const auto name = w->getTextEditorContents("name").trim();
                               if (name.isNotEmpty())
                                   processor_.stateManager().saveUserPreset(name);
                           }
                           refresh();
                       }),
                       true /* delete when dismissed */);
}

void PresetBar::resized() {
    const int h = getHeight();
    const int cy = (h - 24) / 2;
    const int groupW = 24 + 6 + 240 + 6 + 24 + 12 + 56;
    int x = (getWidth() - groupW) / 2;
    prevButton_.setBounds(x, cy + 4, 24, 16);
    x += 30;
    nameButton_.setBounds(x, cy, 240, 24);
    x += 246;
    nextButton_.setBounds(x, cy + 4, 24, 16);
    x += 36;
    saveButton_.setBounds(x, cy + 1, 56, 22);
    bypassButton_.setBounds(getWidth() - 12 - 68, cy + 1, 68, 22);
}

void PresetBar::paint(juce::Graphics& g) {
    // Logo text, medium weight: "FilterTable" + accent "US".
    const juce::Font logoFont = FtusLookAndFeel::font(16.0f, true);
    g.setFont(logoFont);
    const juce::String part1 = "FilterTable";
    const float w1 = juce::GlyphArrangement::getStringWidth(logoFont, part1);
    const auto textArea = getLocalBounds().toFloat().withTrimmedLeft(14.0f);
    g.setColour(theme::text);
    g.drawText(part1, textArea, juce::Justification::centredLeft, false);
    g.setColour(theme::accent);
    g.drawText("US", textArea.withTrimmedLeft(w1 + 1.0f), juce::Justification::centredLeft,
               false);

    g.setColour(theme::stroke);
    g.fillRect(0.0f, static_cast<float>(getHeight()) - 1.0f, static_cast<float>(getWidth()),
               1.0f);
}

} // namespace ftus
