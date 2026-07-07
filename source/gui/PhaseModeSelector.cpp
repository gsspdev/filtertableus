#include "gui/PhaseModeSelector.h"

#include "ftus/PluginIDs.h"

namespace ftus {

namespace {
const char* phaseModeTooltip(int index) {
    switch (index) {
        case 0: return "Minimum phase - zero latency";
        case 1: return "Linear phase - adds L/2 latency (about 21 ms at 48 kHz)";
        case 2: return "Original frame phase - adds L/2 latency (about 21 ms at 48 kHz)";
        case 3: return "Raw frame cycle as causal kernel - zero latency";
        default: return "";
    }
}
} // namespace

PhaseModeSelector::PhaseModeSelector(FtusAudioProcessor& processor, BindingRegistry& registry,
                                     ReadoutSink* sink)
    : sink_(sink) {
    param_ = processor.state().getParameter(ids::phaseMode);
    jassert(param_ != nullptr);

    const auto& choices = phaseModeChoices();
    for (int i = 0; i < choices.size(); ++i) {
        auto* seg = segments_.add(new SegmentButton(choices[i]));
        seg->setTooltip(phaseModeTooltip(i));
        seg->onClick = [this, i] {
            if (attachment_ != nullptr)
                attachment_->setValueAsCompleteGesture(static_cast<float>(i));
        };
        seg->onHoverChanged = [this, i](bool over) {
            if (sink_ == nullptr)
                return;
            if (over)
                sink_->showReadout("Phase Mode", phaseModeChoices()[i]);
            else
                sink_->clearReadout();
        };
        addAndMakeVisible(seg);
    }

    attachment_ = std::make_unique<juce::ParameterAttachment>(
        *param_, [this](float v) { setSelected(juce::roundToInt(v)); }, nullptr);
    attachment_->sendInitialUpdate();
    registry.add(ids::phaseMode);
}

void PhaseModeSelector::setSelected(int index) {
    for (int i = 0; i < segments_.size(); ++i)
        segments_[i]->setActive(i == index);
}

void PhaseModeSelector::resized() {
    auto r = getLocalBounds().reduced(4);
    const int w = r.getWidth() / juce::jmax(1, segments_.size());
    for (auto* seg : segments_)
        seg->setBounds(r.removeFromLeft(w));
}

void PhaseModeSelector::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat(), 5.0f);
}

} // namespace ftus
