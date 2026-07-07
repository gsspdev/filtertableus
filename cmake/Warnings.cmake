# Strict warnings for OUR code only. Never attach this to JUCE, pffft, or other third-party targets.
add_library(ftus_warnings INTERFACE)
add_library(ftus::warnings ALIAS ftus_warnings)

if(MSVC)
  target_compile_options(ftus_warnings INTERFACE /W4 /WX)
else()
  target_compile_options(ftus_warnings INTERFACE
    -Wall -Wextra -Wshadow -Wdouble-promotion -Wreorder -Werror)
endif()
