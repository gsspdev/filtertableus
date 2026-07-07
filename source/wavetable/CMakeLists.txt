# source/wavetable — owned by the wavetable-I/O workstream.
file(GLOB FTUS_WAVETABLE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(ftus_shared INTERFACE ${FTUS_WAVETABLE_SOURCES})
