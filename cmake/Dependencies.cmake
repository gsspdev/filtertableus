# Dependency resolution: prefer the pre-cloned local checkouts in external/ (pinned, offline),
# fall back to FetchContent at the SAME pins if a checkout is missing.
#
# Pins (see also external/PINS.txt):
#   JUCE                  8.0.14   2cdfca8feb300fb424002ba2c2751569e5bacb64
#   clap-juce-extensions           16e9d4ca7b1e86c76e04584b2c08e85a764bcda8  (needs submodules)
#   Catch2                v3.7.1   fa43b77429ba76c462b1898d6cd2f2d7a9416b14
#   pffft (vendored copy in core/third_party/pffft)  a4b03590cc2a4bea56f9721996e3057835799179

include(FetchContent)
set(FTUS_EXTERNAL "${CMAKE_SOURCE_DIR}/external")

if(FTUS_BUILD_PLUGIN)
  if(EXISTS "${FTUS_EXTERNAL}/JUCE/CMakeLists.txt")
    add_subdirectory("${FTUS_EXTERNAL}/JUCE" "${CMAKE_BINARY_DIR}/_deps/juce-build" EXCLUDE_FROM_ALL)
  else()
    FetchContent_Declare(JUCE
      GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
      GIT_TAG 2cdfca8feb300fb424002ba2c2751569e5bacb64)
    FetchContent_MakeAvailable(JUCE)
  endif()

  if(FTUS_ENABLE_CLAP)
    if(EXISTS "${FTUS_EXTERNAL}/clap-juce-extensions/CMakeLists.txt")
      add_subdirectory("${FTUS_EXTERNAL}/clap-juce-extensions" "${CMAKE_BINARY_DIR}/_deps/cje-build" EXCLUDE_FROM_ALL)
    else()
      FetchContent_Declare(clap-juce-extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG 16e9d4ca7b1e86c76e04584b2c08e85a764bcda8)
      FetchContent_MakeAvailable(clap-juce-extensions)
    endif()
  endif()
endif()

if(FTUS_BUILD_TESTS)
  if(EXISTS "${FTUS_EXTERNAL}/Catch2/CMakeLists.txt")
    add_subdirectory("${FTUS_EXTERNAL}/Catch2" "${CMAKE_BINARY_DIR}/_deps/catch2-build" EXCLUDE_FROM_ALL)
    list(APPEND CMAKE_MODULE_PATH "${FTUS_EXTERNAL}/Catch2/extras")
  else()
    FetchContent_Declare(Catch2
      GIT_REPOSITORY https://github.com/catchorg/Catch2.git
      GIT_TAG fa43b77429ba76c462b1898d6cd2f2d7a9416b14)
    FetchContent_MakeAvailable(Catch2)
    list(APPEND CMAKE_MODULE_PATH "${catch2_SOURCE_DIR}/extras")
  endif()
endif()
