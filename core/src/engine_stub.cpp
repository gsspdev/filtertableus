// PHASE 0 STUB implementation of FilterTableEngine: an audio PASSTHROUGH that exercises the
// full facade contract (wavetable handoff, response-curve publication, UI atomics, latency
// reporting) without filtering. The engine-assembly workstream DELETES this file and provides
// core/src/engine.cpp behind the unchanged header.
#include "ftc/FilterTableEngine.h"

#include <atomic>

#include "ftc/EngineConfig.h"
#include "ftc/RealtimeExchange.h"

namespace ftc {

struct FilterTableEngine::Impl {
    PrepareSpec spec{};
    Parameters params{};
    ObjectHandoff<WavetableData> handoff;
    TripleBuffer<ResponseCurve> curve;
    std::atomic<float> scan{0.0f};
    std::atomic<float> env{0.0f};
    std::atomic<int> latency{0};
    WavetablePtr uiTable;       // message-thread-owned mirror for the GUI
    bool prepared = false;
    bool curvePublished = false;

    void publishFlatCurve() {
        ResponseCurve c;
        c.db.fill(0.0f);
        curve.write(c);
        curvePublished = true;
    }
};

FilterTableEngine::FilterTableEngine() : impl_(std::make_unique<Impl>()) {}
FilterTableEngine::~FilterTableEngine() = default;

void FilterTableEngine::prepare(const PrepareSpec& spec) {
    impl_->spec = spec;
    impl_->prepared = true;
    impl_->latency.store(0, std::memory_order_relaxed);
    impl_->publishFlatCurve();
}

void FilterTableEngine::reset() noexcept {}

void FilterTableEngine::setParameters(const Parameters& params) noexcept {
    impl_->params = params;
    impl_->scan.store(params.scan, std::memory_order_relaxed);
}

void FilterTableEngine::setWavetable(WavetablePtr table) {
    impl_->uiTable = table;
    impl_->handoff.publish(std::move(table));
}

void FilterTableEngine::process(float* const* channels, int numChannels, int numSamples,
                                const TransportInfo& transport,
                                std::span<const NoteEvent> notes) noexcept {
    (void)channels; (void)numChannels; (void)numSamples; (void)transport; (void)notes;
    if (!impl_->prepared)
        return;
    impl_->handoff.acquire(); // adopt pending tables so the graveyard advances
    // Passthrough: audio untouched.
}

int FilterTableEngine::latencySamples() const noexcept {
    return impl_->latency.load(std::memory_order_relaxed);
}

// Static design values (true for the real engine; the stub's *instance* latency stays 0).
int FilterTableEngine::latencySamplesFor(PhaseMode mode, double sampleRate) noexcept {
    const int L = EngineConfig::kernelLength(sampleRate);
    return (mode == PhaseMode::Linear || mode == PhaseMode::Original) ? L / 2 : 0;
}

void FilterTableEngine::collectGarbage() {
    impl_->handoff.collectGarbage();
}

bool FilterTableEngine::readResponseCurve(ResponseCurve& out) noexcept {
    return impl_->curve.read(out);
}

float FilterTableEngine::currentScan() const noexcept {
    return impl_->scan.load(std::memory_order_relaxed);
}

float FilterTableEngine::envValue() const noexcept {
    return impl_->env.load(std::memory_order_relaxed);
}

WavetablePtr FilterTableEngine::currentTableForUi() const {
    return impl_->uiTable;
}

} // namespace ftc
