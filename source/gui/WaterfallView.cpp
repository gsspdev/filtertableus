#include "gui/WaterfallView.h"

#include "gui/FtusLookAndFeel.h"
#include "ftus/PluginIDs.h"

namespace ftus {

WaterfallView::WaterfallView(FtusAudioProcessor& processor, BindingRegistry& registry,
                             ReadoutSink* sink)
    : processor_(processor), sink_(sink) {
    scanParam_ = processor_.state().getParameter(ids::scan);
    jassert(scanParam_ != nullptr);
    scanAttachment_ = std::make_unique<juce::ParameterAttachment>(
        *scanParam_,
        [this](float newValue) {
            paramValue_ = newValue;
            paramDirty_ = true; // gate: repaint on the next timer tick
        },
        nullptr);
    scanAttachment_->sendInitialUpdate();
    registry.add(ids::scan);

    setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
    startTimerHz(30);
}

void WaterfallView::resized() { rebuildPaths(); }

void WaterfallView::timerCallback() {
    auto latest = processor_.engine().currentTableForUi();
    if (latest != table_) {
        table_ = std::move(latest);
        rebuildPaths();
        repaint();
        return;
    }
    const float s = displayScan();
    if (std::abs(s - paintedScan_) > 1.0f / 512.0f || paramDirty_) {
        paramDirty_ = false;
        repaint();
    }
}

float WaterfallView::displayScan() const {
    return juce::jlimit(0.0f, 1.0f, processor_.engine().currentScan());
}

WaterfallView::Geometry WaterfallView::computeGeometry(int depths) const {
    const auto b = getLocalBounds().toFloat().reduced(16.0f);
    Geometry g;
    g.depths = juce::jmax(1, depths);
    const float dxTotal = kDx * static_cast<float>(g.depths - 1);
    const float dyTotal = -kDy * static_cast<float>(g.depths - 1);
    g.amplitude = juce::jlimit(10.0f, 64.0f, (b.getHeight() - dyTotal) * 0.5f);
    g.waveWidth = juce::jmax(40.0f, b.getWidth() - dxTotal);
    g.x0 = b.getX();
    const float totalH = dyTotal + 2.0f * g.amplitude;
    g.baseY = b.getBottom() - (b.getHeight() - totalH) * 0.5f - g.amplitude;
    return g;
}

juce::Path WaterfallView::buildFramePath(int frameIndex, float depthSlot) const {
    juce::Path p;
    if (table_ == nullptr)
        return p;
    const auto frame = table_->frame(juce::jlimit(0, table_->numFrames() - 1, frameIndex));
    if (frame.empty())
        return p;
    const float ox = geo_.x0 + depthSlot * kDx;
    const float oy = geo_.baseY + depthSlot * kDy;
    constexpr int stride = ftc::WavetableData::kFrameLength / kPointsPerFrame;
    p.preallocateSpace(3 * kPointsPerFrame + 8);
    for (int k = 0; k < kPointsPerFrame; ++k) {
        const float sample = juce::jlimit(-1.2f, 1.2f, frame[static_cast<size_t>(k * stride)]);
        const float px = ox + geo_.waveWidth * static_cast<float>(k)
                                  / static_cast<float>(kPointsPerFrame - 1);
        const float py = oy - sample * geo_.amplitude;
        if (k == 0)
            p.startNewSubPath(px, py);
        else
            p.lineTo(px, py);
    }
    return p;
}

void WaterfallView::rebuildPaths() {
    framePaths_.clear();
    if (table_ == nullptr || table_->numFrames() <= 0 || getWidth() <= 0 || getHeight() <= 0) {
        geo_ = computeGeometry(1);
        return;
    }
    const int n = table_->numFrames();
    const int depths = juce::jmin(kMaxDepths, n);
    geo_ = computeGeometry(depths);
    framePaths_.reserve(static_cast<size_t>(depths));
    for (int j = 0; j < depths; ++j) {
        const int frameIndex =
            depths <= 1 ? 0
                        : juce::roundToInt(static_cast<double>(j) * (n - 1) / (depths - 1));
        framePaths_.push_back(buildFramePath(frameIndex, static_cast<float>(j)));
    }
}

void WaterfallView::paint(juce::Graphics& g) {
    theme::paintPanel(g, getLocalBounds().toFloat());

    if (table_ == nullptr || framePaths_.empty()) {
        g.setColour(theme::textDim);
        g.setFont(FtusLookAndFeel::font(13.0f));
        g.drawText("No wavetable loaded", getLocalBounds().withTrimmedBottom(24),
                   juce::Justification::centred, true);
        g.setColour(theme::textDim.withAlpha(0.7f));
        g.setFont(FtusLookAndFeel::font(11.0f));
        g.drawText("drop a .wav anywhere, or use Load / Factory above",
                   getLocalBounds().withTrimmedTop(24), juce::Justification::centred, true);
        paintedScan_ = displayScan();
        return;
    }

    const int depths = static_cast<int>(framePaths_.size());
    for (int j = depths - 1; j >= 0; --j) { // back first
        const float depthT =
            depths <= 1 ? 0.0f : static_cast<float>(j) / static_cast<float>(depths - 1);
        g.setColour(theme::wave.withAlpha(1.0f - depthT * 0.82f)); // 1.0 -> 0.18
        g.strokePath(framePaths_[static_cast<size_t>(j)], juce::PathStrokeType(1.0f));
    }

    // Active frame from the engine's post-modulation scan atomic, drawn last in accent with a
    // soft under-fill down to its baseline.
    const float scan = displayScan();
    const int n = table_->numFrames();
    const float framePos = scan * static_cast<float>(n - 1);
    const int frameIndex = juce::roundToInt(framePos);
    const float slot = n <= 1 ? 0.0f : scan * static_cast<float>(depths - 1);

    juce::Path active = buildFramePath(frameIndex, slot);
    if (!active.isEmpty()) {
        juce::Path fill(active);
        const float oy = geo_.baseY + slot * kDy;
        fill.lineTo(geo_.x0 + slot * kDx + geo_.waveWidth, oy + geo_.amplitude * 0.9f);
        fill.lineTo(geo_.x0 + slot * kDx, oy + geo_.amplitude * 0.9f);
        fill.closeSubPath();
        g.setColour(theme::accent.withAlpha(0.13f));
        g.fillPath(fill);
        g.setColour(theme::accent);
        g.strokePath(active, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }

    g.setColour(theme::textDim);
    g.setFont(FtusLookAndFeel::font(10.5f));
    g.drawText(scanText(scan), getLocalBounds().reduced(12, 8),
               juce::Justification::topRight, true);

    paintedScan_ = scan;
}

float WaterfallView::scanForPosition(juce::Point<float> pos) const {
    const float dyTotal = juce::jmax(20.0f, -kDy * static_cast<float>(geo_.depths - 1));
    return juce::jlimit(0.0f, 1.0f, (geo_.baseY - pos.y) / dyTotal);
}

juce::String WaterfallView::scanText(float scan01) const {
    if (table_ != nullptr && table_->numFrames() > 0) {
        const int n = table_->numFrames();
        const int fi = juce::roundToInt(scan01 * static_cast<float>(n - 1)) + 1;
        return "Frame " + juce::String(fi) + "/" + juce::String(n);
    }
    return juce::String(juce::roundToInt(scan01 * 100.0f)) + " %";
}

void WaterfallView::pushReadout(float scan01) {
    if (sink_ != nullptr)
        sink_->showReadout("Scan", scanText(scan01));
}

void WaterfallView::mouseDown(const juce::MouseEvent& e) {
    if (scanAttachment_ == nullptr)
        return;
    dragStartValue_ = paramValue_;
    dragStartPos_ = e.position;
    scanAttachment_->beginGesture();
    if (!e.mods.isShiftDown()) {
        const float v = scanForPosition(e.position);
        scanAttachment_->setValueAsPartOfGesture(v);
        pushReadout(v);
    }
}

void WaterfallView::mouseDrag(const juce::MouseEvent& e) {
    if (scanAttachment_ == nullptr)
        return;
    float v;
    if (e.mods.isShiftDown()) { // fine: 0.1x relative to the drag start
        const float dyTotal = juce::jmax(20.0f, -kDy * static_cast<float>(geo_.depths - 1));
        v = dragStartValue_ + (dragStartPos_.y - e.position.y) / dyTotal * 0.1f;
    } else {
        v = scanForPosition(e.position);
    }
    v = juce::jlimit(0.0f, 1.0f, v);
    scanAttachment_->setValueAsPartOfGesture(v);
    pushReadout(v);
}

void WaterfallView::mouseUp(const juce::MouseEvent&) {
    if (scanAttachment_ != nullptr)
        scanAttachment_->endGesture();
}

void WaterfallView::mouseDoubleClick(const juce::MouseEvent&) {
    if (scanAttachment_ == nullptr || scanParam_ == nullptr)
        return;
    const float def =
        scanParam_->getNormalisableRange().convertFrom0to1(scanParam_->getDefaultValue());
    scanAttachment_->setValueAsCompleteGesture(def);
    pushReadout(def);
}

void WaterfallView::mouseMove(const juce::MouseEvent&) { pushReadout(displayScan()); }

void WaterfallView::mouseExit(const juce::MouseEvent&) {
    if (sink_ != nullptr)
        sink_->clearReadout();
}

} // namespace ftus
