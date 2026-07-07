# source/gui — owned by the GUI workstream. Add files freely; they're picked up on reconfigure.
file(GLOB FTUS_GUI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
target_sources(ftus_shared INTERFACE ${FTUS_GUI_SOURCES})
