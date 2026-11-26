cmake_minimum_required(VERSION 3.31)
project(BagiEngine)

# Engine setup (dependencies, options, modules) and the be_* functions that games build on
include(cmake/BagiEngine.cmake)

if(ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# The engine's own 2D stress app (BagiStress): perf and control-smoke target
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/stress/CMakeLists.txt)
    add_subdirectory(tools/stress)
endif()

# The engine's demo scene (BagiDemo): the game of the editor page
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/tools/demo/CMakeLists.txt)
    add_subdirectory(tools/demo)
endif()

if(MSVC AND TARGET BagiStress)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT BagiStress)
endif()
