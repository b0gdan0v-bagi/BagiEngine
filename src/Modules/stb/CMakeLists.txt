# stb - single-header libraries by Sean Barrett (Vendored)
# stb_truetype.h (v1.26): glyph metrics and rasterization for BECore/Text, compiled in BECore/Text/Font.cpp with
# STB_TRUETYPE_IMPLEMENTATION. stb_vorbis.c (v1.24): the Ogg Vorbis decoder of BECore/Audio, compiled in
# BECore/Audio/AudioDecode.cpp. stb_image.h (v2.32): the PNG reader of the engine, compiled in
# SDL/src/CoreSDL/ImageFile.cpp. Each implementation lives in exactly one translation unit (README.md).

add_library(stb INTERFACE)

target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Group in IDE
set_target_properties(stb PROPERTIES FOLDER "External")
