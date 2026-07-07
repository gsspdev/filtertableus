# source/plugin — FROZEN after Phase 0 (explicit list on purpose; nobody else adds files here).
target_sources(ftus_shared INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/PluginProcessor.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/Parameters.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/WavetableCodecImpl.cpp")
