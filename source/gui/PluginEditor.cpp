// PHASE 0 STUB editor: JUCE's generic parameter panel — every one of the 27 parameters is
// host- and GUI-usable from day 1. The GUI workstream replaces this file (and adds its own
// components in this directory) behind the frozen ftus/EditorFactory.h seam.
#include <juce_audio_utils/juce_audio_utils.h>

#include "ftus/EditorFactory.h"
#include "plugin/PluginProcessor.h"

namespace ftus {

juce::AudioProcessorEditor* createFtusEditor(FtusAudioProcessor& processor) {
    auto* editor = new juce::GenericAudioProcessorEditor(processor);
    editor->setSize(560, 700);
    return editor;
}

} // namespace ftus
