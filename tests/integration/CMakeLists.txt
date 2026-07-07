# tests/integration — owned by the harness workstream.
file(GLOB FTUS_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
add_executable(ftus_integration_tests ${FTUS_INTEGRATION_TEST_SOURCES})
target_link_libraries(ftus_integration_tests PRIVATE ftus_shared Catch2::Catch2WithMain)
include(Catch)
# TIMEOUT guards CI against hangs (renders + the threaded stress test are seconds, not minutes).
catch_discover_tests(ftus_integration_tests PROPERTIES TIMEOUT 600 LABELS integration)
