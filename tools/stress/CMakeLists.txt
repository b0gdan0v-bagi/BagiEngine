# BagiStress
# The engine's own 2D stress app: an IGame with no rules and three scenes (sprites, entities, ui) chosen by argument (CLAUDE.md).
# It lives in tools/, not in games/: games/ goes away when Match3 leaves the repository. Not a game of its own repository, so there is no standalone block.

be_add_module(Stress LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
be_add_game_reflection(StressModule)

be_add_game_executable(BagiStress
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES StressModule
)

if(ENGINE_BUILD_TESTS)
    file(GLOB STRESS_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")
    # WHOLE_ARCHIVE: the tests that load the shipped SceneConfig.xml name components no test references
    be_add_game_tests(StressTests
        SOURCES ${STRESS_TEST_SOURCES}
        WHOLE_ARCHIVE StressModule
    )
endif()
