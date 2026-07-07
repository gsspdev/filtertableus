# ftcore_tests — JUCE-free Catch2 suite. Sources GLOBbed so workstreams add test files
# (test_convolver*.cpp, test_kernel*.cpp, test_mod*.cpp, test_engine*.cpp) without CMake edits.
file(GLOB FTCORE_TEST_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/helpers/*.cpp")

add_executable(ftcore_tests ${FTCORE_TEST_SOURCES})
target_include_directories(ftcore_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(ftcore_tests PRIVATE ftcore Catch2::Catch2WithMain)

include(CTest)
include(Catch)
catch_discover_tests(ftcore_tests)
