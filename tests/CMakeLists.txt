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
    list(FILTER TESTS_SOURCES EXCLUDE REGEX "/BECore/(ConfigEditorSaveTest|SceneEditorSaveTest|ConfigFieldSchemaTest|KeyChordTest|HotkeyMapTest|CommandFilterTest|EditorSettingsTest|ViewportInputTest|LayoutStoreTest|IconTableTest|EditorPlayModelTest|StatusBarModelTest|GitBranchTest|EditorBarsTest|HierarchyModelTest|ScenePickTest|ViewportGizmosTest|GizmoGeometryTest|GizmoDragTest|ViewportCameraTest|SceneSelectionTest|FieldPresentationTest|ConsoleLogViewTest|EditorLauncherTest)\\.")
endif()

# Tests of the RmlUi host exist only when the host is built
if(ENGINE_UI_RMLUI)
    file(GLOB_RECURSE RMLUI_TESTS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.h"
    )
    list(APPEND TESTS_SOURCES ${RMLUI_TESTS_SOURCES})
endif()

# The fuzz targets: a library of its own, because the libFuzzer executables (cmake/BagiFuzz.cmake) link the same functions
add_subdirectory(Fuzz)

be_add_game_tests(BagiEngineTests SOURCES ${TESTS_SOURCES} LINK RemoteControlModule RoomsModule RoomClientModule RoomServerModule RuntimeModule BagiFuzzTargets)

# VulkanSwapchainPolicy.h is header only (<vulkan/vulkan.h>, BECore's EnumUtils.h for SwapchainStatus and
# SwapchainRecreateResult, and the standard library), so the test needs the headers and the include path but not
# VulkanModule: its loader library is not deployed next to the test executable (BagiEngineTests links BECore).
target_link_libraries(BagiEngineTests PRIVATE Vulkan::Headers)
target_include_directories(BagiEngineTests PRIVATE ${BAGI_ENGINE_DIR}/src/Modules/Vulkan/src)

# THIRD_PARTY_NOTICES.md against the tree: a new vendored library, external folder, font or vendored file without its row fails
if(Python3_FOUND)
    add_test(NAME BagiThirdPartyNotices
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_third_party_notices.py --root ${BAGI_ENGINE_DIR})
    # a quoted include is allowed only for a source file's own header; every other include is <...> from an include root
    add_test(NAME BagiIncludeStyle
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_includes.py --root ${BAGI_ENGINE_DIR})
    # the type rules of BECore/Types/CLAUDE.md as a check (raw std::/eastl:: types, allocating calls: CI/scripts/check_types.py).
    # The engine still holds lines that other entries own (file streams, directory iterators, ...), so the check has a baseline,
    # CI/types_baseline.txt: it only shrinks, a fix removes its lines, and a new hit or a line that is gone fails the test.
    add_test(NAME BagiTypesCheck
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_types.py --root ${BAGI_ENGINE_DIR}
            --baseline ${BAGI_ENGINE_DIR}/CI/types_baseline.txt)
    # every header compiles alone (no PCH), and any header that does not fails the test: there is no baseline (CI/scripts/check_headers.py).
    # A fragment by design would be an --exclude here, with its reason in a comment above it; there is none.
    # RmlUiHost is compiled only with ENGINE_UI_RMLUI, so a build without it has no flags for its headers and the check leaves them out;
    # with the option (macos-rmlui-debug) the same test checks them.
    # 4 compiles at once, so no RUN_SERIAL (it would stop the other slots while it waits and runs).
    set(HEADERS_STANDALONE_EXCLUDE)
    if(NOT ENGINE_UI_RMLUI)
        set(HEADERS_STANDALONE_EXCLUDE --exclude src/Modules/RmlUiHost)
    endif()
    add_test(NAME BagiHeadersStandalone
        COMMAND ${Python3_EXECUTABLE} ${BAGI_ENGINE_DIR}/CI/scripts/check_headers.py --root ${BAGI_ENGINE_DIR} --build ${CMAKE_BINARY_DIR}
            ${HEADERS_STANDALONE_EXCLUDE} --jobs 4)
    set_tests_properties(BagiHeadersStandalone PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 300)
endif()

# A real TLS handshake of the platform's backend against local servers: tls_local_check.py starts a Python `ssl` WebSocket echo server
# (fixtures in BECore/Tls/data) and runs TlsLocalEndpointTest of BagiEngineTests with their ports in the environment. Skipped (77) when
# Python has no `ssl`; the cases themselves skip on a platform without a TLS backend. Own port per run (port 0), so it runs beside the others.
if(Python3_FOUND)
    add_test(NAME BagiTlsLocalEndpoint
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/BECore/Tls/tls_local_check.py --exe $<TARGET_FILE:BagiEngineTests>)
    set_tests_properties(BagiTlsLocalEndpoint PROPERTIES
        SKIP_RETURN_CODE 77
        TIMEOUT 120)
endif()
