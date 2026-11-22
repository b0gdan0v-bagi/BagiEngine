# BagiStress
# The engine's own 2D stress app: an IGame with no rules and three scenes (sprites, entities, ui) chosen by argument (CLAUDE.md).
# The engine's own runnable program, in tools/. Not a game of its own repository, so there is no standalone block.

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
    # the control smoke of the app: its own script (a fresh game per scene, the check of the game's log)
    be_add_game_control_tests(BagiStress SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/tests/stress_smoke.py)
    # the cook of the app's content, read back against the XML: every config of the engine, including the widgets
    # that only this executable can make
    be_add_game_cook_test(BagiStress)
    # the recorded control sessions of the app (tests/scenarios/*.jsonl), played by tools/control/bagi_play.py and replayed
    # with the generic checks and qa/invariants.py by tools/qa/bagi_qa.py
    be_add_game_scenario_tests(BagiStress DIR tests/scenarios INVARIANTS qa/invariants.py)
endif()
