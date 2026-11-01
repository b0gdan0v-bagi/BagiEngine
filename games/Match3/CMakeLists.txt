# Match3
# Concrete IGame implementation (pure Bejeweled-style rules + scene component + Match3Game) and the BagiMatch3 executable.

# Standalone build: the game in its own repository, the engine as the git submodule engine/.
# Inside the engine repository the engine is already set up by the root CMakeLists.txt and this block is skipped.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.31)
    project(BagiMatch3)
    include(${CMAKE_CURRENT_SOURCE_DIR}/engine/cmake/BagiEngine.cmake)
endif()

be_add_module(Match3 LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
be_add_game_reflection(Match3Module)

be_add_game_executable(BagiMatch3
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES Match3Module
)

if(ENGINE_BUILD_TESTS)
    file(GLOB MATCH3_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    be_add_game_tests(Match3Tests
        SOURCES ${MATCH3_TEST_SOURCES}
        LINK Match3Module
    )

    # Starts BagiMatch3 with the control API and plays a few moves over WebSocket
    be_add_game_control_tests(BagiMatch3)
endif()
