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
    # BagiDemoScenario.title: sets the title text over the control API and compares the picture with references/title.png
    # (tolerance in the scenario file), the model of a scenario with a reference picture that guide/test-a-game.md excerpts.
    be_add_game_scenario_tests(BagiDemo DIR tests/scenarios)

    # The Python of the guide for people (docs/guide/scripts/, excerpted by the pages) must keep running: one entry per script,
    # BagiDemoGuideScript.<name>, with the lock of every test that starts a game. The path is the engine checkout, which holds the guide.
    # A new script needs no CMake edit. A script takes the game's path first and accepts --cleanup (every globbed script is called so); the record script removes its scenario folder with it.
    file(GLOB GUIDE_SCRIPTS CONFIGURE_DEPENDS "${BAGI_ENGINE_DIR}/docs/guide/scripts/*.py")
    if(NOT Python3_FOUND)
        set(GUIDE_SCRIPTS "")
    endif()
    foreach(GUIDE_SCRIPT IN LISTS GUIDE_SCRIPTS)
        get_filename_component(GUIDE_SCRIPT_NAME ${GUIDE_SCRIPT} NAME_WE)
        set(GUIDE_TEST_NAME BagiDemoGuideScript.${GUIDE_SCRIPT_NAME})
        add_test(NAME ${GUIDE_TEST_NAME} COMMAND ${Python3_EXECUTABLE} ${GUIDE_SCRIPT} $<TARGET_FILE:BagiDemo> --cleanup)
        set_tests_properties(${GUIDE_TEST_NAME} PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 120
            RESOURCE_LOCK bagi_control_smoke
            LABELS smoke
        )
    endforeach()
endif()
