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
    list(FILTER TESTS_SOURCES EXCLUDE REGEX "/BECore/(ConfigEditorSaveTest|SceneEditorSaveTest|ConfigFieldSchemaTest|KeyChordTest|HotkeyMapTest|CommandFilterTest|EditorSettingsTest|LayoutStoreTest)\\.")
endif()

# Tests of the RmlUi host exist only when the host is built
if(ENGINE_UI_RMLUI)
    file(GLOB_RECURSE RMLUI_TESTS_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/RmlUiHost/*.h"
    )
    list(APPEND TESTS_SOURCES ${RMLUI_TESTS_SOURCES})
endif()

be_add_game_tests(BagiEngineTests SOURCES ${TESTS_SOURCES} LINK RemoteControlModule RoomsModule RuntimeModule)

# VulkanSwapchainPolicy.h is header only (<vulkan/vulkan.h>, BECore's EnumUtils.h for SwapchainStatus and
# SwapchainRecreateResult, and the standard library), so the test needs the headers and the include path but not
# VulkanModule: its loader library is not deployed next to the test executable (BagiEngineTests links BECore).
target_link_libraries(BagiEngineTests PRIVATE Vulkan::Headers)
target_include_directories(BagiEngineTests PRIVATE ${BAGI_ENGINE_DIR}/src/Modules/Vulkan/src)
