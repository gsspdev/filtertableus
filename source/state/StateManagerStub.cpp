// PHASE 0 STUB state manager: APVTS-only session state (parameters round-trip; no wavetable
// embedding, no presets). The state workstream deletes this file and provides
// StateManagerImpl.cpp behind the frozen ftus/StateManager.h seam.
#include "ftus/StateManager.h"

#include "plugin/PluginProcessor.h"

namespace ftus {

namespace {

class StateManagerStub : public StateManager {
public:
    explicit StateManagerStub(FtusAudioProcessor& p) : processor_(p) {}

    void getState(juce::MemoryBlock& dest) override {
        juce::ValueTree root("FilterTableUS");
        root.setProperty("stateVersion", 1, nullptr);
        root.appendChild(processor_.state().copyState(), nullptr);
        juce::MemoryOutputStream out(dest, false);
        root.writeToStream(out);
    }

    void setState(const void* data, int sizeInBytes) override {
        const auto root = juce::ValueTree::readFromData(data, static_cast<size_t>(sizeInBytes));
        if (!root.isValid() || !root.hasType("FilterTableUS"))
            return;
        const auto params = root.getChildWithName(processor_.state().state.getType());
        if (params.isValid())
            processor_.state().replaceState(params);
    }

    juce::StringArray listPresets() override { return {}; }
    bool loadPreset(const juce::String&) override { return false; }
    bool saveUserPreset(const juce::String&) override { return false; }
    bool nextPreset() override { return false; }
    bool prevPreset() override { return false; }
    juce::String currentPresetName() override { return "Init"; }
    bool isDirty() override { return false; }

private:
    FtusAudioProcessor& processor_;
};

} // namespace

std::unique_ptr<StateManager> createStateManager(FtusAudioProcessor& processor) {
    return std::make_unique<StateManagerStub>(processor);
}

} // namespace ftus
