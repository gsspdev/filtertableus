# tests/unit/state — owned by the state/presets workstream.
file(GLOB FTUS_STATE_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(ftus_state_tests ${FTUS_STATE_TEST_SOURCES})
target_link_libraries(ftus_state_tests PRIVATE ftus_shared Catch2::Catch2WithMain)
include(Catch)
catch_discover_tests(ftus_state_tests)
