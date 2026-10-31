# Tests CMake Configuration
# Standalone GoogleTest executable. The runtime ITest framework (BECore/Tests) is separate and runs inside the game.
# Common/ is the shared test support library (BagiEngineTestSupport), built by cmake/BagiEngine.cmake.

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)
list(FILTER TESTS_SOURCES EXCLUDE REGEX "/Common/")

be_add_game_tests(BagiEngineTests
    SOURCES ${TESTS_SOURCES}
    LINK ChessModule Match3Module
)
