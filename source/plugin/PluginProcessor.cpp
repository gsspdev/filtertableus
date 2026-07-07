#include "plugin/PluginProcessor.h"

#include "ftus/EditorFactory.h"
#include "ftus/PluginIDs.h"

namespace ftus {

FtusAudioProcessor::FtusAudioProcessor()
    : juce::AudioProcessor(BusesProperties()
                               .withInput("Input", juce::AudioChannelSet::stereo(), true)
                               .withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "PARAMS", createParameterLayout()) {
    cacheParameterPointers();
    apvts_.addParameterListener(ids::phaseMode, this);

    loader_ = createLoaderService([this](const LoadResult& result) {
        // Message thread by LoaderService contract.
        if (result.ok && result.table != nullptr) {
            lastLoadError_.clear();
            adoptWavetable(result.table, result.info);
        } else {
            lastLoadError_ = result.errorMessage;
            sendChangeMessage();
        }
    });
    stateManager_ = createStateManager(*this);

    noteScratch_.reserve(256);
    startTimer(1000); // engine graveyard GC
}

FtusAudioProcessor::~FtusAudioProcessor() {
    stopTimer();
    cancelPendingUpdate();
    apvts_.removeParameterListener(ids::phaseMode, this);
}

void FtusAudioProcessor::cacheParameterPointers() {
    auto get = [this](const char* id) { return apvts_.getRawParameterValue(id); };
    raw_.scan = get(ids::scan);
    raw_.cutoff = get(ids::cutoff);
    raw_.resonance = get(ids::resonance);
    raw_.mix = get(ids::mix);
    raw_.phaseMode = get(ids::phaseMode);
    raw_.keytrack = get(ids::keytrack);
    raw_.outGain = get(ids::outGain);
    raw_.bypass = get(ids::bypass);
    raw_.lfoRate[0] = get(ids::lfo1Rate);       raw_.lfoRate[1] = get(ids::lfo2Rate);
    raw_.lfoSync[0] = get(ids::lfo1Sync);       raw_.lfoSync[1] = get(ids::lfo2Sync);
    raw_.lfoDiv[0] = get(ids::lfo1Div);         raw_.lfoDiv[1] = get(ids::lfo2Div);
    raw_.lfoShape[0] = get(ids::lfo1Shape);     raw_.lfoShape[1] = get(ids::lfo2Shape);
    raw_.lfoRetrig[0] = get(ids::lfo1Retrig);   raw_.lfoRetrig[1] = get(ids::lfo2Retrig);
    raw_.lfoToScan[0] = get(ids::lfo1ToScan);   raw_.lfoToScan[1] = get(ids::lfo2ToScan);
    raw_.lfoToCutoff[0] = get(ids::lfo1ToCutoff); raw_.lfoToCutoff[1] = get(ids::lfo2ToCutoff);
    raw_.envSens = get(ids::envSens);
    raw_.envAttack = get(ids::envAttack);
    raw_.envRelease = get(ids::envRelease);
    raw_.envToScan = get(ids::envToScan);
    raw_.envToCutoff = get(ids::envToCutoff);
}

void FtusAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock) {
    engine_.prepare({sampleRate, samplesPerBlock, juce::jmax(1, getTotalNumOutputChannels())});
    setLatencySamples(engine_.latencySamples());
}

bool FtusAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const {
    const auto in = layouts.getMainInputChannelSet();
    const auto out = layouts.getMainOutputChannelSet();
    if (in != out)
        return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void FtusAudioProcessor::fillParameters(ftc::Parameters& out) const noexcept {
    auto ld = [](std::atomic<float>* p) { return p->load(std::memory_order_relaxed); };
    out.scan = ld(raw_.scan);
    out.cutoffHz = ld(raw_.cutoff);
    out.resonance = ld(raw_.resonance);
    out.mix = ld(raw_.mix);
    out.mode = static_cast<ftc::PhaseMode>(static_cast<int>(ld(raw_.phaseMode)));
    out.keytrack = ld(raw_.keytrack);
    out.outGainDb = ld(raw_.outGain);
    ftc::LfoParams* lfos[2] = {&out.lfo1, &out.lfo2};
    for (int i = 0; i < 2; ++i) {
        lfos[i]->rateHz = ld(raw_.lfoRate[i]);
        lfos[i]->tempoSync = ld(raw_.lfoSync[i]) > 0.5f;
        lfos[i]->division = static_cast<ftc::SyncDivision>(static_cast<int>(ld(raw_.lfoDiv[i])));
        lfos[i]->shape = static_cast<ftc::LfoShape>(static_cast<int>(ld(raw_.lfoShape[i])));
        lfos[i]->retrigger = ld(raw_.lfoRetrig[i]) > 0.5f;
        lfos[i]->toScan = ld(raw_.lfoToScan[i]);
        lfos[i]->toCutoff = ld(raw_.lfoToCutoff[i]);
    }
    out.env.sensitivityDb = ld(raw_.envSens);
    out.env.attackMs = ld(raw_.envAttack);
    out.env.releaseMs = ld(raw_.envRelease);
    out.env.toScan = ld(raw_.envToScan);
    out.env.toCutoff = ld(raw_.envToCutoff);
}

void FtusAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) {
    juce::ScopedNoDenormals noDenormals;

    if (raw_.bypass->load(std::memory_order_relaxed) > 0.5f)
        return; // parameter bypass: pass audio through untouched

    fillParameters(paramScratch_);

    noteScratch_.clear(); // keeps capacity — no allocation
    for (const auto metadata : midi) {
        const auto msg = metadata.getMessage();
        if (noteScratch_.size() >= noteScratch_.capacity())
            break;
        if (msg.isNoteOn())
            noteScratch_.push_back({metadata.samplePosition,
                                    static_cast<std::uint8_t>(msg.getNoteNumber()),
                                    static_cast<std::uint8_t>(msg.getVelocity()), true});
        else if (msg.isNoteOff())
            noteScratch_.push_back({metadata.samplePosition,
                                    static_cast<std::uint8_t>(msg.getNoteNumber()),
                                    static_cast<std::uint8_t>(msg.getVelocity()), false});
    }

    ftc::TransportInfo transport;
    if (auto* playhead = getPlayHead()) {
        if (const auto pos = playhead->getPosition()) {
            transport.bpm = pos->getBpm().orFallback(120.0);
            transport.ppqPosition = pos->getPpqPosition().orFallback(0.0);
            transport.playing = pos->getIsPlaying();
            transport.valid = true;
        }
    }

    engine_.setParameters(paramScratch_);
    engine_.process(buffer.getArrayOfWritePointers(), buffer.getNumChannels(),
                    buffer.getNumSamples(), transport,
                    {noteScratch_.data(), noteScratch_.size()});
}

juce::AudioProcessorEditor* FtusAudioProcessor::createEditor() {
    return createFtusEditor(*this);
}

void FtusAudioProcessor::getStateInformation(juce::MemoryBlock& destData) {
    stateManager_->getState(destData);
}

void FtusAudioProcessor::setStateInformation(const void* data, int sizeInBytes) {
    stateManager_->setState(data, sizeInBytes);
}

juce::AudioProcessorParameter* FtusAudioProcessor::getBypassParameter() const {
    return apvts_.getParameter(ids::bypass);
}

void FtusAudioProcessor::adoptWavetable(ftc::WavetablePtr table, const TableSourceInfo& info) {
    engine_.setWavetable(std::move(table));
    tableInfo_ = info;
    sendChangeMessage(); // post-based; safe off the message thread
}

void FtusAudioProcessor::parameterChanged(const juce::String& parameterID, float) {
    if (parameterID == ids::phaseMode)
        triggerAsyncUpdate();
}

void FtusAudioProcessor::handleAsyncUpdate() {
    setLatencySamples(engine_.latencySamples());
    updateHostDisplay(ChangeDetails{}.withLatencyChanged(true));
}

void FtusAudioProcessor::timerCallback() {
    engine_.collectGarbage();
}

} // namespace ftus

// JUCE plugin entry point.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() {
    return new ftus::FtusAudioProcessor();
}
