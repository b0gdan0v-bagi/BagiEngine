# Tests CMake Configuration
# Standalone GoogleTest executable for the engine. The runtime ITest framework (BECore/Tests) is separate and runs
# inside the game. Common/ is the shared test support library (BagiEngineTestSupport), built by cmake/BagiEngine.cmake.
# Game tests live next to their game (games/<Name>/tests).

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.h"
)

be_add_game_tests(BagiEngineTests SOURCES ${TESTS_SOURCES})
