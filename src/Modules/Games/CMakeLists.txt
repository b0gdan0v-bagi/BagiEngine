# Games Module CMake Configuration
# Базовая инфраструктура под несколько игр (IGame, GameManager, реестр).

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

file(GLOB_RECURSE GAMES_MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(GamesModule STATIC ${GAMES_MODULE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Games" FILES ${GAMES_MODULE_SOURCES})

set_target_properties(GamesModule PROPERTIES FOLDER "Modules")

# Reuse PCH from BECoreModule.
target_precompile_headers(GamesModule REUSE_FROM BECoreModule)

if(MSVC)
    target_compile_options(GamesModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(GamesModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(GamesModule PRIVATE -include "BECore/pch.h")
endif()

target_include_directories(GamesModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${PROJECT_ROOT}/src/Modules/Games/src
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(GamesModule PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)

target_link_libraries(GamesModule PUBLIC
    BECoreModule
    EventsModule
)
