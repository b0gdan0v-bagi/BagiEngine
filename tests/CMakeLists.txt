# Tests CMake Configuration
# Standalone GoogleTest executable for the engine. The runtime ITest framework (BECore/Tests) is separate and runs
# inside the game. Common/ is the shared test support library (BagiEngineTestSupport), built by cmake/BagiEngine.cmake.
# Game tests live next to their game (games/<Name>/tests).

file(GLOB_RECURSE TESTS_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/BECore/*.h"
)

# Tests of the editor panels exist only when the editor is built (BE_WITH_EDITOR, cmake/BagiEngine.cmake)
if(NOT BE_EDITOR_ENABLED)
    list(FILTER TESTS_SOURCES EXCLUDE REGEX "/BECore/(ConfigEditorSaveTest|SceneEditorSaveTest|ConfigFieldSchemaTest|KeyChordTest|HotkeyMapTest|CommandFilterTest|EditorSettingsTest|ViewportInputTest|LayoutStoreTest|IconTableTest|EditorPlayModelTest|StatusBarModelTest|GitBranchTest|EditorBarsTest|HierarchyModelTest)\\.")
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
