# Runtime Module CMake Configuration
# Application loop and the widgets (editor UI). Every game executable links it with WHOLE_ARCHIVE
# (be_add_game_executable), because widgets register themselves in AbstractFactory<IWidget>
# from static initializers. src/main.cpp is not part of the library: each game executable compiles it.

file(GLOB_RECURSE RUNTIME_MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Application/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Widgets/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/Widgets/*.h"
)

add_library(RuntimeModule STATIC ${RUNTIME_MODULE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Runtime" FILES ${RUNTIME_MODULE_SOURCES})

set_target_properties(RuntimeModule PROPERTIES FOLDER "Modules")

# Reuse PCH from BECoreModule.
target_precompile_headers(RuntimeModule REUSE_FROM BECoreModule)
be_force_include_pch(RuntimeModule)

# Widgets reflection is handled by BECoreModule (src/Widgets is in its scan_dirs); the generated
# headers are reached through ${CMAKE_BINARY_DIR}.
target_include_directories(RuntimeModule PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules
    ${CMAKE_BINARY_DIR}
)

# IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
target_compile_definitions(RuntimeModule PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)

target_link_libraries(RuntimeModule PUBLIC
    BECoreModule
    EventsModule
    TaskSystemModule
    RemoteControlModule
    GamesModule
    SDLModule
    VulkanModule
)
