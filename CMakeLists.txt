cmake_minimum_required(VERSION 3.31)
project(BagiEngine)

# Engine setup (dependencies, options, modules) and the be_* functions that games build on
include(cmake/BagiEngine.cmake)

if(ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Games that live in this repository. Each one reaches the engine only through cmake/BagiEngine.cmake,
# so a game can move to its own repository without changes; once all games are gone, so is games/.
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/games/CMakeLists.txt)
    add_subdirectory(games)
endif()

if(MSVC AND TARGET BagiChess)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BagiChess)
endif()
