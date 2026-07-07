#include "gui/ModPanel.h"

#include "gui/FtusLookAndFeel.h"
#include "ftus/PluginIDs.h"

namespace ftus {

namespace {

struct LfoIdSet {
    const char* rate;
    const char* sync;
    const char* div;
    const char* shape;
    const char* retrig;
    const char* toScan;
    const char* toCutoff;
};

LfoIdSet lfoIds(int which) {
    if (which == 0)
        return {ids::lfo1Rate, ids::lfo1Sync, ids::lfo1Div, ids::lfo1Shape,
                ids::lfo1Retrig, ids::lfo1ToScan, ids::lfo1ToCutoff};
    return {ids::lfo2Rate, ids::lfo2Sync, ids::lfo2Div, ids::lfo2Shape,
            ids::lfo2Retrig, ids::lfo2ToScan, ids::lfo2ToCutoff};
}

void fillChoiceBox(juce::ComboBox& box, juce::AudioProcessorValueTreeState& apvts,
                   const char* paramId) {
    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(apvts.getParameter(paramId)))
        box.addItemList(choice->choices, 1);
}

} // namespace

// ------------------------------------------------------------------- LfoPage

LfoPage::LfoPage(juce::AudioProcessorValueTreeState& apvts, int which,
                 BindingRegistry& registry, ReadoutSink* sink)
    : rate_(apvts, lfoIds(which).rate, "Rate", 40, false, sink, registry),
      toScan_(apvts, lfoIds(which).toScan, "> Scan", 40, true, sink, registry),
      toCutoff_(apvts, lfoIds(which).toCutoff, "> Cutoff", 40, true, sink, registry) {
    const auto set = lfoIds(which);

    addAndMakeVisible(rate_);
    addAndMakeVisible(toScan_);
    addAndMakeVisible(toCutoff_);

    fillChoiceBox(divBox_, apvts, set.div);
    divBox_.setTooltip("Tempo-sync division");
    addAndMakeVisible(divBox_);
    divAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, set.div, divBox_);
    registry.add(set.div);

    fillChoiceBox(shapeBox_, apvts, set.shape);
    shapeBox_.setTooltip("LFO shape");
    addAndMakeVisible(shapeBox_);
    shapeAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, set.shape, shapeBox_);
    registry.add(set.shape);

    syncButton_.setTooltip("Sync the LFO rate to the host tempo");
    addAndMakeVisible(syncButton_);
    syncAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, set.sync, syncButton_);
    registry.add(set.sync);

    retrigButton_.setTooltip("Reset LFO phase on MIDI note-on");
    addAndMakeVisible(retrigButton_);
    retrigAttachment_ = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, set.retrig, retrigButton_);
    registry.add(set.retrig);

    rate_.slider().setTooltip("LFO rate (free-running)");
    // rate/toScan/toCutoff were registered by their LabeledKnob constructors.

    // Rate knob <-> Division combo occupy the same slot; the Sync toggle swaps them.
    // onStateChange fires for attachment-driven changes too (host automation).
    syncButton_.onStateChange = [this] { updateRateDivVisibility(); };
    updateRateDivVisibility();
}

void LfoPage::updateRateDivVisibility() {
    const bool sync = syncButton_.getToggleState();
    rate_.setVisible(!sync);
    divBox_.setVisible(sync);
}

void LfoPage::resized() {
    const int h = getHeight();
    rate_.setBounds(2, 0, 84, h - 2);
    divBox_.setBounds(8, (h - 22) / 2 - 7, 72, 22);
    syncButton_.setBounds(100, h / 2 - 22, 64, 19);
    retrigButton_.setBounds(100, h / 2 + 3, 64, 19);
    shapeBox_.setBounds(178, (h - 22) / 2, 96, 22);
    toScan_.setBounds(292, 0, 76, h - 2);
    toCutoff_.setBounds(372, 0, 76, h - 2);
}

// ------------------------------------------------------------------- EnvPage

void EnvMeter::paint(juce::Graphics& g) {
    auto b = getLocalBounds().toFloat();
    auto labelArea = b.removeFromBottom(14.0f);
    auto track = b.withSizeKeepingCentre(10.0f, b.getHeight() - 4.0f);
    const auto trackOutline = track;
    g.setColour(theme::control);
    g.fillRoundedRectangle(track, 4.0f);
    auto fill = track.removeFromBottom(track.getHeight() * value_);
    if (fill.getHeight() > 0.5f) {
        g.setColour(theme::accent);
        g.fillRoundedRectangle(fill, 4.0f);
    }
    g.setColour(theme::stroke);
    g.drawRoundedRectangle(trackOutline.reduced(0.5f), 4.0f, 1.0f);
    g.setColour(theme::textDim);
    g.setFont(FtusLookAndFeel::font(10.0f));
    g.drawText("ENV", labelArea, juce::Justification::centred, false);
}

EnvPage::EnvPage(FtusAudioProcessor& processor, BindingRegistry& registry, ReadoutSink* sink)
    : sens_(processor.state(), ids::envSens, "Sens", 40, true, sink, registry),
      attack_(processor.state(), ids::envAttack, "Attack", 40, false, sink, registry),
      release_(processor.state(), ids::envRelease, "Release", 40, false, sink, registry),
      toScan_(processor.state(), ids::envToScan, "> Scan", 40, true, sink, registry),
      toCutoff_(processor.state(), ids::envToCutoff, "> Cutoff", 40, true, sink, registry),
      meter_(processor) {
    addAndMakeVisible(sens_);
    addAndMakeVisible(attack_);
    addAndMakeVisible(release_);
    addAndMakeVisible(toScan_);
    addAndMakeVisible(toCutoff_);
    addAndMakeVisible(meter_);
}

void EnvPage::resized() {
    const int h = getHeight();
    sens_.setBounds(2, 0, 76, h - 2);
    attack_.setBounds(82, 0, 76, h - 2);
    release_.setBounds(162, 0, 76, h - 2);
    meter_.setBounds(252, 2, 28, h - 4);
    toScan_.setBounds(292, 0, 76, h - 2);
    toCutoff_.setBounds(372, 0, 76, h - 2);
}

// ------------------------------------------------------------------- ModPanel

ModPanel::ModPanel(FtusAudioProcessor& processor, BindingRegistry& registry, ReadoutSink* sink)
    : lfo1_(processor.state(), 0, registry, sink),
      lfo2_(processor.state(), 1, registry, sink),
      env_(processor, registry, sink) {
    const char* names[] = {"LFO 1", "LFO 2", "ENV"};
    for (int i = 0; i < 3; ++i) {
        auto* tab = tabs_.add(new SegmentButton(names[i], true));
        tab->onClick = [this, i] { setActiveTab(i); };
        addAndMakeVisible(tab);
    }
    addChildComponent(lfo1_);
    addChildComponent(lfo2_);
    addChildComponent(env_);
    setActiveTab(0);
}

void ModPanel::setActiveTab(int index) {
    activeTab_ = index;
    for (int i = 0; i < tabs_.size(); ++i)
        tabs_[i]->setActive(i == index);
    lfo1_.setVisible(index == 0);
    lfo2_.setVisible(index == 1);
    env_.setVisible(index == 2);
}

void ModPanel::resized() {
    auto r = getLocalBounds().reduced(8, 2);
    auto tabCol = r.removeFromLeft(76);
    const int tabH = 20;
    int y = tabCol.getY() + 3;
    for (auto* tab : tabs_) {
        tab->setBounds(tabCol.getX(), y, tabCol.getWidth(), tabH);
        y += tabH + 3;
    }
    r.removeFromLeft(12);
    lfo1_.setBounds(r);
    lfo2_.setBounds(r);
    env_.setBounds(r);
}

void ModPanel::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat().reduced(6.0f, 0.0f));
    // Divider between the tab column and the page content.
    g.setColour(theme::stroke);
    g.fillRect(92.0f, 8.0f, 1.0f, static_cast<float>(getHeight()) - 16.0f);
}

} // namespace ftus
