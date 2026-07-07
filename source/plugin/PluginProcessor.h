// FilterTableUS shell — the AudioProcessor. FROZEN after Phase 0 (only the Wave-3 integration
// agent may amend). All feature work happens behind the seams: ftc::FilterTableEngine (core),
// createFtusEditor (gui), createLoaderService (wavetable), createStateManager (state).
#pragma once
#include <atomic>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>

#include "ftc/FilterTableEngine.h"
#include "ftc/Parameters.h"
#include "ftus/LoaderService.h"
#include "ftus/StateManager.h"
#include "ftus/WavetableCodec.h"

namespace ftus {

class FtusAudioProcessor : public juce::AudioProcessor,
                           public juce::ChangeBroadcaster, // fires on table/preset changes
                           private juce::AudioProcessorValueTreeState::Listener,
                           private juce::AsyncUpdater,
                           private juce::Timer {
public:
    FtusAudioProcessor();
    ~FtusAudioProcessor() override;

    // juce::AudioProcessor
    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    void processBlockBypassed(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "FilterTableUS"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.05; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    // FilterTableUS surface (message thread unless stated otherwise)
    juce::AudioProcessorValueTreeState& state() noexcept { return apvts_; }
    ftc::FilterTableEngine& engine() noexcept { return engine_; }
    LoaderService& loader() noexcept { return *loader_; }
    StateManager& stateManager() noexcept { return *stateManager_; }
    const TableSourceInfo& currentTableInfo() const noexcept { return tableInfo_; }
    const juce::String& lastLoadError() const noexcept { return lastLoadError_; }

    /// Adopt a loaded/decoded wavetable: engine handoff + info bookkeeping + change broadcast.
    /// Message thread (or the host's setStateInformation thread — broadcast is post-based).
    void adoptWavetable(ftc::WavetablePtr table, const TableSourceInfo& info);

private:
    void processInternal(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                         bool forceBypass);
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override; // latency renotify on the message thread
    void timerCallback() override;     // ~1 Hz engine garbage collection
    void fillParameters(ftc::Parameters& out) const noexcept;
    void cacheParameterPointers();

    juce::AudioProcessorValueTreeState apvts_;
    ftc::FilterTableEngine engine_;
    std::unique_ptr<LoaderService> loader_;
    std::unique_ptr<StateManager> stateManager_;
    TableSourceInfo tableInfo_;
    juce::String lastLoadError_;

    struct Raw {
        std::atomic<float>* scan = nullptr;
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* resonance = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* phaseMode = nullptr;
        std::atomic<float>* keytrack = nullptr;
        std::atomic<float>* outGain = nullptr;
        std::atomic<float>* bypass = nullptr;
        std::atomic<float>* lfoRate[2] = {};
        std::atomic<float>* lfoSync[2] = {};
        std::atomic<float>* lfoDiv[2] = {};
        std::atomic<float>* lfoShape[2] = {};
        std::atomic<float>* lfoRetrig[2] = {};
        std::atomic<float>* lfoToScan[2] = {};
        std::atomic<float>* lfoToCutoff[2] = {};
        std::atomic<float>* envSens = nullptr;
        std::atomic<float>* envAttack = nullptr;
        std::atomic<float>* envRelease = nullptr;
        std::atomic<float>* envToScan = nullptr;
        std::atomic<float>* envToCutoff = nullptr;
    } raw_;

    std::vector<ftc::NoteEvent> noteScratch_; // capacity reserved in prepareToPlay
    ftc::Parameters paramScratch_{};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FtusAudioProcessor)
};

} // namespace ftus
