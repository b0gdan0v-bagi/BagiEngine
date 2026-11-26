# Tests CMake Configuration
# Standalone GoogleTest executable for the engine. The runtime ITest framework (BECore/Tests) is separate and runs
# inside the game. Common/ is the shared test support library (BagiEngineTestSupport), built by cmake/BagiEngine.cmake.
# A game's tests live in the repository of the game.

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.h"
)

# Tests of the editor panels exist only when the editor is built (BE_WITH_EDITOR, cmake/BagiEngine.cmake)
if(NOT BE_EDITOR_ENABLED)
    list(FILTER TESTS_SOURCES EXCLUDE REGEX "/BECore/(ConfigEditorSaveTest|ConfigEditorPanelModelTest|ConfigEditorAssetTest|SceneEditorSaveTest|ConfigFieldSchemaTest|KeyChordTest|HotkeyMapTest|CommandFilterTest|EditorSettingsTest|EditorLayoutHostTest|PreferencesPanelModelTest|PreferencesPanelAssetTest|EditorDialogsModelTest|SceneDialogsModelTest|DialogsAssetTest|ViewportInputTest|LayoutStoreTest|IconTableTest|EditorPlayModelTest|StatusBarModelTest|GitBranchTest|EditorBarsTest|MainToolbarModelTest|MainToolbarAssetTest|MainMenuModelTest|MainMenuAssetTest|EditorCommandArgumentTest|HardwareTextTest|HardwareMonitorTest|HardwareReadersTest|HardwareMacMappingTest|HierarchyModelTest|ScenePickTest|ViewportGizmosTest|ViewportUIGizmosTest|ViewportToolbarModelTest|ViewportToolbarAssetTest|GizmoGeometryTest|GizmoDragTest|ViewportCameraTest|SceneSelectionTest|FieldPresentationTest|ConsoleLogViewTest|EditorLauncherTest|ConsoleCommandLineTest|ConsolePanelModelTest|ConsolePanelAssetTest|ProfilerPanelModelTest|ProfilerPanelAssetTest|FileChangesModelTest|GamePanelModelTest|SceneEditorPanelModelTest|SceneEditorAssetTest|ArtGenPanelModelTest|ArtGenPanelAssetTest|ArtGenSessionTest|ArtGenAcceptTest|ArtGenLaunchTest|ArtGenPresetFormTest|ArtGenInputValueTest|ArtGenReferenceTest|ArtGenSoundTest|ArtGenImageTest|ArtGenSetTest|ArtGenSetStoreTest|ArtGenSetSessionTest|ArtGenAtlasAcceptTest|ArtGenSetPanelTest|ArtGenVerdictTest|SpriteSheetGridTest|SpriteImageTest|SpriteSheetIslandsTest|SpriteAtlasPackerTest|SpriteAtlasTest|SpriteLibraryViewTest|SpriteAtlasPreviewTest|SpriteLibraryPanelModelTest|SpriteLibraryAssetTest|PanelModelTest|PanelBarsTest|FileChangesPanelModelTest|GamePanelAssetModelTest|GamePanelAssetTest|PanelInterpreterTest|PanelAssetHotReloadTest|PanelAssetShippedTest|EditorDocumentsProviderTest|AssetCatalogTest|AssetBrowserPanelModelTest|AssetBrowserAssetTest|AssetDropInspectorTest|SceneAssetDropTest|AssetReferencesTest|AssetFileOpsTest|ExportLaunchTest|ExportLogTest|ExportPanelModelTest|ExportAssetTest)\\.")
endif()

# Tests of the RmlUi host exist only when the host is built
if(ENGINE_UI_RMLUI)
    file(GLOB_RECURSE RMLUI_TESTS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.h"
    )
    list(APPEND TESTS_SOURCES ${RMLUI_TESTS_SOURCES})
endif()

# A wrong LOG_X format must not compile: each case of CompileFail/LogFormat.cpp is an object target outside "all", and the ctest builds them
# one after the other (CheckCompileFail.cmake; the control case must build, the others must fail). One test, because two concurrent
# `cmake --build` of one build folder race on the ninja log.
set(LOG_FORMAT_CASES ok error error error)
set(LOG_FORMAT_CASE_TARGETS)
foreach(CASE_INDEX RANGE 0 3)
    list(GET LOG_FORMAT_CASES ${CASE_INDEX} CASE_EXPECTED)
    set(CASE_TARGET BagiLogFormatCase${CASE_INDEX})
    add_library(${CASE_TARGET} OBJECT EXCLUDE_FROM_ALL CompileFail/LogFormat.cpp)
    target_compile_definitions(${CASE_TARGET} PRIVATE LOG_FORMAT_CASE=${CASE_INDEX})
    target_link_libraries(${CASE_TARGET} PRIVATE BECoreModule)
    list(APPEND LOG_FORMAT_CASE_TARGETS "${CASE_TARGET}:${CASE_EXPECTED}")
endforeach()
add_test(NAME BagiLogFormatCompileFail
    COMMAND ${CMAKE_COMMAND} -DBUILD_DIR=${CMAKE_BINARY_DIR} -DCONFIG=$<CONFIG> "-DCASE_TARGETS=${LOG_FORMAT_CASE_TARGETS}"
        -P ${CMAKE_CURRENT_SOURCE_DIR}/CompileFail/CheckCompileFail.cmake)
# RUN_SERIAL: with a stale tree the build relinks BECoreModule and its dependants in the folder whose executables the other tests are running.
set_tests_properties(BagiLogFormatCompileFail PROPERTIES RUN_SERIAL TRUE TIMEOUT 300)

# The fuzz targets: a library of its own, because the libFuzzer executables (cmake/BagiFuzz.cmake) link the same functions
add_subdirectory(Fuzz)

# The set of native programs the engine's tests and benchmark run: the formulas, the tool that writes their C++ and the generated file the next line
# gives BagiEngineTests (tests/NativePrograms/CMakeLists.txt)
add_subdirectory(NativePrograms)

be_add_game_tests(BagiEngineTests SOURCES ${TESTS_SOURCES} LINK RemoteControlModule RoomsModule RoomClientModule RoomServerModule RuntimeModule BagiFuzzTargets BagiNativeProgramSet)
be_add_native_programs(BagiEngineTests GENERATOR BagiNativeProgramsTool)

# The tests that compare the bits of a program with plain C++ compile without floating-point contraction, as the interpreter does
# (src/Modules/BECore/CMakeLists.txt): in Release the compiler fuses the reference's multiply and add, which rounds once, and the
# interpreter's two instructions do not (Lerp of a large float: -inf against -1.7e38)
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/ProgramInterpreterTest.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/ValueProgramLoadTest.cpp"
    PROPERTIES COMPILE_OPTIONS "$<IF:$<CXX_COMPILER_ID:MSVC>,/fp:precise,-ffp-contract=off>"
)

# The FuzzCorpus cases of the shards only replay the corpus; this entry runs them again with the mutations (tests/Fuzz/CLAUDE.md). 50000 per target
# of cost weight 1 is about 2 s in Debug; the targets run one after the other in this one process.
# A filter that matches nothing (a renamed instantiation) exits 0, and the vendored gtest has no --gtest_fail_if_no_test_selected: the
# count line fails it instead. FAIL_, not PASS_REGULAR_EXPRESSION, which would stop ctest from reading the exit code.
add_test(NAME BagiFuzzMutations COMMAND BagiEngineTests --gtest_filter=FuzzCorpus/* --gtest_brief=1)
set_tests_properties(BagiFuzzMutations PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FAIL_REGULAR_EXPRESSION "\\] 0 tests from 0 test suites ran"
    ENVIRONMENT "BAGI_FUZZ_ITERATIONS=50000"
    LABELS extended)

# The cases over the real OS watch backend wait for the platform (fseventsd drops events on a busy machine and a case sits out its wait), so the
# shards and the perf run skip them (tests/Common/NativeFileWatch.h) and this entry runs them, with the polling and facade cases of the same files.
# No test preset runs the label: `ctest --test-dir build/<preset> -L native-watch` after a change of the backends (.claude/rules/todo-project.md)
add_test(NAME BagiNativeFileWatch COMMAND BagiEngineTests --gtest_filter=FileWatchBackend*:FileWatcherFSEventsRescanTest.* --gtest_brief=1)
set_tests_properties(BagiNativeFileWatch PROPERTIES
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    FAIL_REGULAR_EXPRESSION "\\] 0 tests from 0 test suites ran"
    ENVIRONMENT "BAGI_NATIVE_FILEWATCH_TESTS=1"
    LABELS native-watch)

# VulkanSwapchainPolicy.h is header only (<vulkan/vulkan.h>, BECore's EnumUtils.h for SwapchainStatus and
# SwapchainRecreateResult, and the standard library), so the test needs the headers and the include path but not
# VulkanModule: its loader library is not deployed next to the test executable (BagiEngineTests links BECore).
target_link_libraries(BagiEngineTests PRIVATE Vulkan::Headers)
target_include_directories(BagiEngineTests PRIVATE ${BAGI_ENGINE_DIR}/src/Modules/Vulkan/src)

# THIRD_PARTY_NOTICES.md against the tree: a new vendored library, external folder, font or vendored file without its row fails
if(Python3_FOUND)
    add_test(NAME BagiThirdPartyNotices
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_third_party_notices.py --root ${BAGI_ENGINE_DIR})
    # the fragments of changelog/, CHANGELOG.md and the version header agree (tools/changelog/CLAUDE.md)
    add_test(NAME BagiChangelogCheck
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/tools/changelog/changelog.py check --root ${BAGI_ENGINE_DIR})
    # a quoted include is allowed only for a source file's own header; every other include is <...> from an include root
    add_test(NAME BagiIncludeStyle
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_includes.py --root ${BAGI_ENGINE_DIR})
    # the constants rules of PoolString/CLAUDE.md, "Constants in code" (no literal in Intern/Find, one place per text, at most two loose
    # "x"_intern per file, none in a header): CI/scripts/check_pool_strings.py; the allowlist rows each carry a reason and a stale one fails
    add_test(NAME BagiPoolStrings
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_pool_strings.py --root ${BAGI_ENGINE_DIR}
            --allowlist ${BAGI_ENGINE_DIR}/CI/pool_strings_allowlist.txt)
    # the type rules of BECore/Types/CLAUDE.md as a check (raw std::/eastl:: types, allocating calls: CI/scripts/check_types.py).
    # The engine still holds lines that other entries own (file streams, directory iterators, ...), so the check has a baseline,
    # CI/types_baseline.txt: it only shrinks, a fix removes its lines, and a new hit or a line that is gone fails the test.
    add_test(NAME BagiTypesCheck
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_types.py --root ${BAGI_ENGINE_DIR}
            --baseline ${BAGI_ENGINE_DIR}/CI/types_baseline.txt)
    # no LOG_X(Format(...)) / LOG_X("..."_format(...)) is left: LOG_X("x {}", v) is the form (the family `log` of tools/codemod, which also does the migration).
    add_test(NAME BagiLogCalls
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/tools/codemod/codemod.py --root ${BAGI_ENGINE_DIR} --family log --fail-on-change)
    # every header compiles alone (no PCH), and any header that does not fails the test: there is no baseline (CI/scripts/check_headers.py).
    # A fragment by design would be an --exclude here, with its reason in a comment above it; there is none.
    # RmlUiHost is compiled only with ENGINE_UI_RMLUI, so a build without it has no flags for its headers and the check leaves them out;
    # with the option (macos-rmlui-debug) the same test checks them.
    # 4 compiles at once, so no RUN_SERIAL (it would stop the other slots while it waits and runs).
    set(HEADERS_STANDALONE_EXCLUDE)
    if(NOT ENGINE_UI_RMLUI)
        set(HEADERS_STANDALONE_EXCLUDE --exclude src/Modules/RmlUiHost --exclude tests/RmlUiHost)
    endif()
    add_test(NAME BagiHeadersStandalone
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_headers.py --root ${BAGI_ENGINE_DIR} --build ${CMAKE_BINARY_DIR}
            ${HEADERS_STANDALONE_EXCLUDE} --jobs 4)
    set_tests_properties(BagiHeadersStandalone PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 1800)  # it compiles every header beside the other tests: only a hang may end it
    # The hygiene checks read the whole tree and none of them tests behaviour: the full run (<preset>-all) has them, the everyday one does not
    set_tests_properties(BagiThirdPartyNotices BagiIncludeStyle BagiPoolStrings BagiTypesCheck BagiLogCalls BagiHeadersStandalone PROPERTIES LABELS extended)
endif()

# A real TLS handshake of the platform's backend against local servers: tls_local_check.py starts a Python `ssl` WebSocket echo server
# (fixtures in BECore/Tls/data) and runs TlsLocalEndpointTest of BagiEngineTests with their ports in the environment. Skipped (77) when
# Python has no `ssl`; the cases themselves skip on a platform without a TLS backend. Own port per run (port 0), so it runs beside the others.
if(Python3_FOUND)
    add_test(NAME BagiTlsLocalEndpoint
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/BECore/Tls/tls_local_check.py --exe $<TARGET_FILE:BagiEngineTests>)
    set_tests_properties(BagiTlsLocalEndpoint PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120
        LABELS extended)
endif()
