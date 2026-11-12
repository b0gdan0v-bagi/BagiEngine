# BagiFuzzTargets
# The fuzz targets (one plain function per input kind), the deterministic mutator and the writer of the generated seeds. FuzzCorpusTest
# (tests/BECore) replays the corpus through them in every test run; cmake/BagiFuzz.cmake wraps the same functions in libFuzzer executables.
# tests/Fuzz/CLAUDE.md says how to add a target.

file(GLOB FUZZ_TARGET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/*.h"
)

add_library(BagiFuzzTargets STATIC ${FUZZ_TARGET_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Fuzz" FILES ${FUZZ_TARGET_SOURCES})

set_target_properties(BagiFuzzTargets PROPERTIES FOLDER "Tests")

# Reuse PCH from BECoreModule, same as BagiEngineTestSupport.
target_precompile_headers(BagiFuzzTargets REUSE_FROM BECoreModule)
be_force_include_pch(BagiFuzzTargets)

# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
target_compile_definitions(BagiFuzzTargets PRIVATE
    IMGUI_VULKAN_AVAILABLE=1
    REFLECTION_GENERATED=1
)

# The corpus folder is public: FuzzCorpusTest, which is in BagiEngineTests, reads the seeds from it.
target_compile_definitions(BagiFuzzTargets PUBLIC "BAGI_FUZZ_CORPUS_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/corpus\"")

# The directory above Fuzz/ is public so tests can include <Fuzz/FuzzTargets.h>; ${CMAKE_BINARY_DIR} is for <Generated/*.gen.hpp>.
get_filename_component(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
target_include_directories(BagiFuzzTargets PUBLIC ${TESTS_DIR} ${CMAKE_BINARY_DIR})

target_link_libraries(BagiFuzzTargets PUBLIC BECoreModule)
if(ENGINE_WITH_REMOTE_CONTROL)
    target_link_libraries(BagiFuzzTargets PUBLIC RemoteControlModule RoomsModule RoomServerModule)
endif()
