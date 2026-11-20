# BagiNativeProgramSet and BagiNativeProgramsTool
# The engine's own native programs: a set of formulas over the reflected test types of tests/BECore, the tool that compiles them and writes the C++ of their
# functions, and (tests/CMakeLists.txt) the generated file BagiEngineTests links. The sources are outside the glob of tests/BECore, so the set is no test case.

file(GLOB NATIVE_SET_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/NativeProgramSet*"
)

add_library(BagiNativeProgramSet STATIC ${NATIVE_SET_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "NativePrograms" FILES ${NATIVE_SET_SOURCES})
set_target_properties(BagiNativeProgramSet PROPERTIES FOLDER "Tests")

target_precompile_headers(BagiNativeProgramSet REUSE_FROM BECoreModule)
be_force_include_pch(BagiNativeProgramSet)

# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
target_compile_definitions(BagiNativeProgramSet PRIVATE
    IMGUI_VULKAN_AVAILABLE=1
    REFLECTION_GENERATED=1
)

# The directory above NativePrograms/ is public: the set includes the reflected test types as <BECore/ValueTestTypes.h> and a user the set as
# <NativePrograms/NativeProgramSet.h>; ${CMAKE_BINARY_DIR} is for <Generated/*.gen.hpp>.
get_filename_component(TESTS_DIR "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
target_include_directories(BagiNativeProgramSet PUBLIC ${TESTS_DIR} ${CMAKE_BINARY_DIR})
target_link_libraries(BagiNativeProgramSet PUBLIC BECoreModule)

# The tool registers the reflection of the test types itself (NativeProgramsTypes.cpp); BagiEngineTests has it in the sources of its own tests, and a
# second copy of a .gen.hpp in one executable is a duplicate definition, so the library holds none
be_add_headless_executable(BagiNativeProgramsTool
    SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/NativeProgramsTool.cpp ${CMAKE_CURRENT_SOURCE_DIR}/NativeProgramsTypes.cpp
    LINK BagiNativeProgramSet
)
set_target_properties(BagiNativeProgramsTool PROPERTIES FOLDER "Tests")
