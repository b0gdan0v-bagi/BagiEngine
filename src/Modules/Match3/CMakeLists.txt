# Match3 Module CMake Configuration
# Concrete IGame implementation: pure Bejeweled-style rules + scene component + Match3Game.

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

file(GLOB_RECURSE MATCH3_MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(Match3Module STATIC ${MATCH3_MODULE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Match3" FILES ${MATCH3_MODULE_SOURCES})

set_target_properties(Match3Module PROPERTIES FOLDER "Modules")

# Reuse PCH from BECoreModule.
target_precompile_headers(Match3Module REUSE_FROM BECoreModule)

if(MSVC)
    target_compile_options(Match3Module PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(Match3Module PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(Match3Module PRIVATE -include "BECore/pch.h")
endif()

target_include_directories(Match3Module PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${PROJECT_ROOT}/src/Modules/Match3/src
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(Match3Module PRIVATE REFLECTION_GENERATED=1)

target_link_libraries(Match3Module PUBLIC
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
