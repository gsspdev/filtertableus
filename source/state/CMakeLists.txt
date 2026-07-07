# source/state — owned by the state/presets workstream.
file(GLOB FTUS_STATE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(ftus_shared INTERFACE ${FTUS_STATE_SOURCES})
