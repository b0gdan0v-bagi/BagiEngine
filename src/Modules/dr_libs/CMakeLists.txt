# dr_libs - single-header audio libraries by David Reid (Vendored)
# dr_flac.h (v0.13.4): the FLAC decoder of BECore/Audio, compiled in BECore/Audio/AudioDecode.cpp with DR_FLAC_IMPLEMENTATION and
# every function static (README.md).

add_library(dr_libs INTERFACE)

target_include_directories(dr_libs INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Group in IDE
set_target_properties(dr_libs PROPERTIES FOLDER "External")
