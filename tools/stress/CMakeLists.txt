# BagiStress
# The engine's own 2D stress app: an IGame with no rules and three scenes (sprites, entities, ui; with ENGINE_UI_RMLUI a fourth, rmlui) chosen by argument (CLAUDE.md).
# The engine's own runnable program, in tools/. Not a game of its own repository, so there is no standalone block.

be_add_module(Stress LINK
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
be_add_game_reflection(StressModule)

# The scene `rmlui` (many RmlUi contexts: texture churn) exists only in a build with the RmlUi host; PUBLIC so StressTests and BagiStress see both
if(TARGET RmlUiHostModule)
    target_compile_definitions(StressModule PUBLIC STRESS_WITH_RMLUI=1)
    target_link_libraries(StressModule PUBLIC RmlUiHostModule)
endif()

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
