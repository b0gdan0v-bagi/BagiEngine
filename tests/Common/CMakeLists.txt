# BagiEngineTestSupport
# Shared by every gtest executable (engine and games): main(), the engine environment and the assert
# bridge. Executables are created with be_add_game_tests() (cmake/BagiEngine.cmake).

file(GLOB TEST_SUPPORT_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

# BagiTestPch: the PCH of every gtest target: the engine's PCH (BECore/pch.h) and <gtest/gtest.h>, so the test sources do not parse gtest
# each. Its flags (the definitions below, the usage requirements of BECoreModule) are those of the tests, or the PCH is rejected
# (-Winvalid-pch). The library has one empty source and nobody links it: be_add_game_tests, BagiEngineTestSupport and so a game's tests reuse
# its PCH. The source is in tests/Pch/ because check_headers.py takes the flags of the headers of this folder from the sources beside them.
# What is not a test (the modules, BagiFuzzTargets) keeps the PCH of BECoreModule, which has no gtest.
add_library(BagiTestPch STATIC "${BAGI_ENGINE_DIR}/tests/Pch/TestPch.cpp")
set_target_properties(BagiTestPch PROPERTIES FOLDER "Tests")
target_precompile_headers(BagiTestPch PRIVATE
    ${BAGI_ENGINE_DIR}/src/Modules/BECore/pch.h
    <gtest/gtest.h>
)
# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the PCH is rejected.
target_compile_definitions(BagiTestPch PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
target_link_libraries(BagiTestPch PUBLIC BECoreModule GTest::gtest)

add_library(BagiEngineTestSupport STATIC ${TEST_SUPPORT_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Common" FILES ${TEST_SUPPORT_SOURCES})

set_target_properties(BagiEngineTestSupport PROPERTIES FOLDER "Tests")

# Reuse the PCH of BagiTestPch (the engine's PCH and gtest)
target_precompile_headers(BagiEngineTestSupport REUSE_FROM BagiTestPch)
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
