// FilterTableUS shell — session state + preset management interface. FROZEN after Phase 0.
// Implemented by the state workstream (source/state/StateManagerImpl.cpp); Phase 0 ships an
// APVTS-only stub (source/state/StateManagerStub.cpp).
//
// Threading: getState/setState may be called from any non-audio thread (hosts do); no GUI
// calls without a MessageManager guard. Preset operations are message-thread-only.
#pragma once
#include <memory>

#include <juce_core/juce_core.h>

namespace ftus {

class FtusAudioProcessor;

class StateManager {
public:
    virtual ~StateManager() = default;

    virtual void getState(juce::MemoryBlock& dest) = 0;
    virtual void setState(const void* data, int sizeInBytes) = 0;

    virtual juce::StringArray listPresets() = 0;               // factory first, then user
    virtual bool loadPreset(const juce::String& name) = 0;
    virtual bool saveUserPreset(const juce::String& name) = 0;
    virtual bool nextPreset() = 0;                             // wraps
    virtual bool prevPreset() = 0;                             // wraps
    virtual juce::String currentPresetName() = 0;
    virtual bool isDirty() = 0;                                // param/table changed since load
};

/// Factory — implemented in source/state/.
std::unique_ptr<StateManager> createStateManager(FtusAudioProcessor& processor);

} // namespace ftus
