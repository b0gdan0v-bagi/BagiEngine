# Tests CMake Configuration
# Standalone GoogleTest executable. The runtime ITest framework (BECore/Tests) is separate and runs inside the game.

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_executable(BagiEngineTests ${TESTS_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests" FILES ${TESTS_SOURCES})

set_target_properties(BagiEngineTests PROPERTIES FOLDER "Tests")

# Reuse PCH from BECoreModule, same as the other targets.
target_precompile_headers(BagiEngineTests REUSE_FROM BECoreModule)

# Test sources include engine headers that rely on the PCH without including it themselves.
if(MSVC)
    target_compile_options(BagiEngineTests PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(BagiEngineTests PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(BagiEngineTests PRIVATE -include "BECore/pch.h")
endif()

# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
# REFLECTION_GENERATED enables the generated reflection code (<Generated/*.gen.hpp>).
target_compile_definitions(BagiEngineTests PRIVATE
    IMGUI_VULKAN_AVAILABLE=1
    REFLECTION_GENERATED=1
)

# ${CMAKE_BINARY_DIR} for <Generated/*.gen.hpp>, ${CMAKE_CURRENT_SOURCE_DIR} for <BECore/...Test.h> and <Common/...>
target_include_directories(BagiEngineTests PRIVATE
    ${CMAKE_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# WHOLE_ARCHIVE keeps the static-initializer registrations (AbstractFactory) from being dead-stripped,
# the same way MyGame links the engine modules.
target_link_libraries(BagiEngineTests PRIVATE
    $<LINK_LIBRARY:WHOLE_ARCHIVE,BECoreModule>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,EventsModule>
    $<LINK_LIBRARY:WHOLE_ARCHIVE,TaskSystemModule>
    ChessModule
    Match3Module
    GamesModule
    GTest::gtest
)

if(APPLE)
    # libSDL3 is written next to the executables (CMAKE_LIBRARY_OUTPUT_DIRECTORY)
    set_target_properties(BagiEngineTests PROPERTIES
        BUILD_RPATH "@executable_path"
        INSTALL_RPATH "@executable_path"
    )
endif()

if(WIN32)
    foreach(_sdl_target SDL3-shared SDL3_image-shared)
        if(TARGET ${_sdl_target})
            add_custom_command(TARGET BagiEngineTests POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:${_sdl_target}>
                $<TARGET_FILE_DIR:BagiEngineTests>
            )
        endif()
    endforeach()
endif()

# Each test runs in its own process, so the global engine state (PoolString table, event dispatchers)
# cannot leak between tests. Tests run in the default working directory (build/<preset>/tests), which
# is inside the repository: FileSystem finds config/ by walking up from the working directory.
# Do not pass WORKING_DIRECTORY here, neither as an argument nor inside PROPERTIES (the parser treats
# both as the same keyword): it also decides where the discovery output (cmake_test_discovery_*.json)
# is written, and pointing it at the repository root litters the source tree.
include(GoogleTest)
gtest_discover_tests(BagiEngineTests DISCOVERY_MODE PRE_TEST)
