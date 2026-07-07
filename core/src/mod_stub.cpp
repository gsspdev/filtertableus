// PHASE 0 STUB implementation of ModulationEngine: zero modulation. The modulation
// workstream DELETES this file and provides core/src/mod.cpp behind the unchanged header.
#include "ftc/Modulation.h"

namespace ftc {

struct ModulationEngine::Impl {
    Parameters params{};
};

ModulationEngine::ModulationEngine() : impl_(std::make_unique<Impl>()) {}
ModulationEngine::~ModulationEngine() = default;

void ModulationEngine::prepare(double sampleRate, int controlInterval) {
    (void)sampleRate; (void)controlInterval;
}

void ModulationEngine::reset() noexcept {}

void ModulationEngine::setParams(const Parameters& params) noexcept {
    impl_->params = params;
}

void ModulationEngine::beginBlock(const TransportInfo& transport, std::span<const NoteEvent> notes,
                                  const float* monoInput, int numSamples) noexcept {
    (void)transport; (void)notes; (void)monoInput; (void)numSamples;
}

ModValues ModulationEngine::evaluate(int subBlockOffset) noexcept {
    (void)subBlockOffset;
    return {};
}

float ModulationEngine::envValue() const noexcept { return 0.0f; }

} // namespace ftc
