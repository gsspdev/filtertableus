# tests/unit/wavetable — owned by the wavetable-I/O workstream.
file(GLOB FTUS_WT_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(ftus_wavetable_tests ${FTUS_WT_TEST_SOURCES})
target_link_libraries(ftus_wavetable_tests PRIVATE ftus_shared Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(ftus_wavetable_tests)
