# BagiEngineTestSupport
# Shared by every gtest executable (engine and games): main(), the engine environment and the assert
# bridge. Executables are created with be_add_game_tests() (cmake/BagiEngine.cmake).

file(GLOB TEST_SUPPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(BagiEngineTestSupport STATIC ${TEST_SUPPORT_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Common" FILES ${TEST_SUPPORT_SOURCES})

set_target_properties(BagiEngineTestSupport PROPERTIES FOLDER "Tests")

# Reuse PCH from BECoreModule, same as the other targets.
target_precompile_headers(BagiEngineTestSupport REUSE_FROM BECoreModule)
be_force_include_pch(BagiEngineTestSupport)

# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
target_compile_definitions(BagiEngineTestSupport PRIVATE
    IMGUI_VULKAN_AVAILABLE=1
    REFLECTION_GENERATED=1
    "BAGI_ENGINE_ROOT=\"${BAGI_ENGINE_DIR}\""
)

# The directory above Common/ is public so tests can include <Common/EngineEnvironment.h>.
get_filename_component(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
target_include_directories(BagiEngineTestSupport PUBLIC ${TESTS_DIR} ${CMAKE_BINARY_DIR})

target_link_libraries(BagiEngineTestSupport PUBLIC
    BECoreModule
    EventsModule
    TaskSystemModule
    GTest::gtest
)
