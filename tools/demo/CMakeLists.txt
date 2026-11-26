# BagiDemo
# The engine's demo scene: an IGame with no rules and one scene of the engine's components, made to be looked at and edited (CLAUDE.md).
# A program of the engine repository, in tools/. Not a game of its own repository, so there is no standalone block.

be_add_module(Demo LINK
    BECoreModule
    EventsModule
    GamesModule
)
be_add_game_reflection(DemoModule)

be_add_game_executable(BagiDemo
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES DemoModule
)

if(ENGINE_BUILD_TESTS)
    file(GLOB DEMO_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: the tests that load the shipped SceneConfig.xml name components no test references
    be_add_game_tests(DemoTests
        SOURCES ${DEMO_TEST_SOURCES}
        WHOLE_ARCHIVE DemoModule
    )
    be_add_game_control_tests(BagiDemo)
    be_add_game_cook_test(BagiDemo)
endif()
