# ftcore — JUCE-free DSP core static library.
# Sources are GLOBbed with CONFIGURE_DEPENDS so parallel workstreams add/replace files in
# core/src/ without editing this file (ownership by filename prefix: convolver* / kernel* /
# wavetabledata* / mod* / engine* / fft / fastmath).
file(GLOB_RECURSE FTCORE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(FTCORE_PFFFT
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft/pffft.c"
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/pffft/pffft_common.c")

add_library(ftcore STATIC ${FTCORE_SOURCES} ${FTCORE_PFFFT})
add_library(ftus::core ALIAS ftcore)

target_include_directories(ftcore
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")

set_target_properties(ftcore PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(NOT MSVC)
  # No -ffast-math anywhere (NaN guards must work); these two are safe.
  target_compile_options(ftcore PRIVATE -fno-math-errno -ffp-contract=fast)
  # Third-party C compiles warning-free without our strict flags.
  set_source_files_properties(${FTCORE_PFFFT} PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_link_libraries(ftcore PRIVATE ftus::warnings)
