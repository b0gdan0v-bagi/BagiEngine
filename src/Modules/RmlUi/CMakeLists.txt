# RmlUi - HTML/CSS-like UI library (Vendored, 6.3)
# https://github.com/mikke89/RmlUi, MIT. Only the core is vendored: Include/RmlUi/{Core,Config,Core.h} and Source/Core, unedited.
# Left out: the Debugger (its fonts are under the SIL OFL), the Lua, SVG and Lottie plugins, the backends, samples and tests.
# The upstream CMake files are replaced by this one; it is added by cmake/BagiEngine.cmake behind ENGINE_UI_RMLUI.

# The font engine: "none" (the default here: the host, src/Modules/RmlUiHost, registers a FontEngineInterface over the engine's own fonts, so nothing
# but the engine is needed) or "freetype" (RmlUi's own engine, needs FreeType installed; the host does not use it, it installs its own)
set(RMLUI_FONT_ENGINE "none" CACHE STRING "RmlUi font engine: none or freetype")
set_property(CACHE RMLUI_FONT_ENGINE PROPERTY STRINGS none freetype)

file(GLOB_RECURSE RMLUI_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/*.cpp")
file(GLOB_RECURSE RMLUI_FONT_ENGINE_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/Source/Core/FontEngineDefault/*.cpp")
list(REMOVE_ITEM RMLUI_SOURCES ${RMLUI_FONT_ENGINE_SOURCES})

if(RMLUI_FONT_ENGINE STREQUAL "freetype")
    find_package(Freetype REQUIRED)
    list(APPEND RMLUI_SOURCES ${RMLUI_FONT_ENGINE_SOURCES})
elseif(NOT RMLUI_FONT_ENGINE STREQUAL "none")
    message(FATAL_ERROR "RMLUI_FONT_ENGINE must be freetype or none, got '${RMLUI_FONT_ENGINE}'")
endif()

add_library(rmlui_core STATIC ${RMLUI_SOURCES})
add_library(RmlUi::Core ALIAS rmlui_core)

target_include_directories(rmlui_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Include)

# RmlUi's public headers use C++17
target_compile_features(rmlui_core PUBLIC cxx_std_17)

# A static library: the headers must not declare __declspec(dllimport)
target_compile_definitions(rmlui_core PUBLIC RMLUI_STATIC_LIB)

# The engine builds without exceptions. robin_hood.h detects that itself; itlib::flat_map needs the define to assert instead of throw.
# The define is public because flat_map.hpp is included from RmlUi's public headers.
target_compile_definitions(rmlui_core PUBLIC ITLIB_FLAT_MAP_NO_THROW)

target_compile_definitions(rmlui_core PRIVATE RMLUI_VERSION="6.3")

if(RMLUI_FONT_ENGINE STREQUAL "freetype")
    target_compile_definitions(rmlui_core PRIVATE RMLUI_FONT_ENGINE_FREETYPE)
    target_link_libraries(rmlui_core PRIVATE Freetype::Freetype)
endif()

# Disable warnings for external library
if(MSVC)
    target_compile_options(rmlui_core PRIVATE /W0 /D_CRT_SECURE_NO_WARNINGS)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(rmlui_core PRIVATE -w)
endif()

# Group in IDE
set_target_properties(rmlui_core PROPERTIES FOLDER "External")
