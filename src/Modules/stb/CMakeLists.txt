# stb - single-header libraries by Sean Barrett (Vendored)
# Only stb_truetype.h (v1.26, public domain) is used: glyph metrics and rasterization for BECore/Text.
# The implementation is compiled in exactly one translation unit (BECore/Text/Font.cpp) with
# STB_TRUETYPE_IMPLEMENTATION; every other file would include the header for declarations only.

add_library(stb INTERFACE)

target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Group in IDE
set_target_properties(stb PROPERTIES FOLDER "External")
