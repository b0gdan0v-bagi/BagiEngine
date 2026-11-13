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

# The engine's own 2D stress app (BagiStress): perf and control-smoke target. In tools/, not games/: games/ goes away with Match3
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/stress/CMakeLists.txt)
    add_subdirectory(tools/stress)
endif()

if(MSVC AND TARGET BagiMatch3)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BagiMatch3)
endif()
