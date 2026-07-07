# Plugin target + the ftus_shared INTERFACE library (pamplejuce pattern).
#
# ftus_shared carries all plugin sources (added by per-directory CMakeLists via
# target_sources(ftus_shared INTERFACE ...)), the JUCE modules, ftcore, include paths,
# and binary-data targets. Both the plugin target and test executables link ftus_shared,
# so each final binary compiles one self-contained copy — this is what lets Catch2 tests
# instantiate the real AudioProcessor headlessly.
#
# OWNERSHIP: this file is frozen after Phase 0. Sources are added ONLY in per-directory
# CMakeLists (source/plugin, source/wavetable, source/gui, source/state).

add_library(ftus_shared INTERFACE)

target_include_directories(ftus_shared INTERFACE
  "${CMAKE_SOURCE_DIR}/include"
  "${CMAKE_SOURCE_DIR}/source")

target_compile_definitions(ftus_shared INTERFACE
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0
  JUCE_DISPLAY_SPLASH_SCREEN=0
  JUCE_MODAL_LOOPS_PERMITTED=1)

# Binary data: file lists are GLOBbed so owning agents drop files in without touching CMake.
file(GLOB FTUS_ASSET_FILES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/resources/fonts/*.ttf"
  "${CMAKE_SOURCE_DIR}/resources/fonts/*.otf"
  "${CMAKE_SOURCE_DIR}/resources/fonts/placeholder.txt")
juce_add_binary_data(ftus_assets HEADER_NAME FtusAssets.h NAMESPACE FtusAssets SOURCES ${FTUS_ASSET_FILES})

file(GLOB FTUS_PRESET_FILES CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/resources/presets/factory/*.ftpreset"
  "${CMAKE_SOURCE_DIR}/resources/presets/factory/placeholder.txt")
juce_add_binary_data(ftus_presets HEADER_NAME FtusPresets.h NAMESPACE FtusPresets SOURCES ${FTUS_PRESET_FILES})

set_target_properties(ftus_assets ftus_presets PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_link_libraries(ftus_shared INTERFACE
  ftcore
  ftus_assets
  ftus_presets
  juce::juce_audio_utils
  juce::juce_dsp
  juce::juce_audio_formats
  juce::juce_gui_extra
  juce::juce_recommended_config_flags)

juce_add_plugin(FilterTableUS
  COMPANY_NAME "FilterTableUS Project"
  BUNDLE_ID "com.filtertableus.FilterTableUS"
  PLUGIN_MANUFACTURER_CODE FtUs
  PLUGIN_CODE Ftbl
  FORMATS VST3 AU Standalone
  PRODUCT_NAME "FilterTableUS"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  AU_MAIN_TYPE kAudioUnitType_MusicEffect
  VST3_CATEGORIES Fx Filter
  COPY_PLUGIN_AFTER_BUILD TRUE
  MICROPHONE_PERMISSION_ENABLED TRUE
  MICROPHONE_PERMISSION_TEXT "FilterTableUS standalone needs audio input.")

target_link_libraries(FilterTableUS PRIVATE ftus_shared)

if(FTUS_ENABLE_CLAP AND COMMAND clap_juce_extensions_plugin)
  clap_juce_extensions_plugin(TARGET FilterTableUS
    CLAP_ID "com.filtertableus.filtertable"
    CLAP_FEATURES audio-effect filter stereo)
endif()
