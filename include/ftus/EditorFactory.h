// FilterTableUS shell — editor factory seam. FROZEN after Phase 0.
// The frozen PluginProcessor calls this; the GUI workstream owns the implementation
// (source/gui/PluginEditor.cpp — Phase 0 stub returns a GenericAudioProcessorEditor).
#pragma once

namespace juce { class AudioProcessorEditor; }

namespace ftus {

class FtusAudioProcessor;

juce::AudioProcessorEditor* createFtusEditor(FtusAudioProcessor& processor);

} // namespace ftus
