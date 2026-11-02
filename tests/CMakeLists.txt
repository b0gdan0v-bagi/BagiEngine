# Tests CMake Configuration
# Standalone GoogleTest executable for the engine. The runtime ITest framework (BECore/Tests) is separate and runs
# inside the game. Common/ is the shared test support library (BagiEngineTestSupport), built by cmake/BagiEngine.cmake.
# Game tests live next to their game (games/<Name>/tests).

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.h"
)

# Tests of the RmlUi host exist only when the host is built
if(ENGINE_UI_RMLUI)
    file(GLOB_RECURSE RMLUI_TESTS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.h"
    )
    list(APPEND TESTS_SOURCES ${RMLUI_TESTS_SOURCES})
endif()

be_add_game_tests(BagiEngineTests SOURCES ${TESTS_SOURCES} LINK RemoteControlModule RuntimeModule)
