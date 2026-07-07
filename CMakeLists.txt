cmake_minimum_required(VERSION 3.24)

# Native arch by default for fast dev loops; pass -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
# for universal release builds. Must be set before project().
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "macOS architectures")
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS")

project(FilterTableUS VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(FTUS_BUILD_TESTS "Build unit/integration tests" ON)
option(FTUS_BUILD_PLUGIN "Build the JUCE plugin targets (OFF = ftcore-only, no JUCE at all)" ON)
option(FTUS_ENABLE_CLAP "Build the CLAP wrapper via clap-juce-extensions" ON)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

add_subdirectory(core)

if(FTUS_BUILD_PLUGIN)
  include(cmake/PluginSetup.cmake)
  add_subdirectory(source/plugin)
  add_subdirectory(source/wavetable)
  add_subdirectory(source/gui)
  add_subdirectory(source/state)
endif()

if(FTUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(core/tests)
  if(FTUS_BUILD_PLUGIN)
    add_subdirectory(tests/unit/wavetable)
    add_subdirectory(tests/unit/state)
    add_subdirectory(tests/integration)
  endif()
endif()
