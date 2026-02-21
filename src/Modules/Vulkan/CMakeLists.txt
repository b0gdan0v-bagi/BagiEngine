# Vulkan Renderer Module
# Provides VulkanRendererBackend implementing IRenderer
#
# Uses vendored Vulkan-Headers (external/Vulkan-Headers) for <vulkan/vulkan.h>.
# Links against the vendored Vulkan loader from external/Vulkan-Loader/.
# See external/Vulkan-Loader/README.md for instructions on populating the binaries.

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

set(VULKAN_LOADER_ROOT "${PROJECT_ROOT}/external/Vulkan-Loader")

file(GLOB_RECURSE VULKAN_MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(VulkanModule STATIC ${VULKAN_MODULE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Vulkan" FILES ${VULKAN_MODULE_SOURCES})
set_target_properties(VulkanModule PROPERTIES FOLDER "Modules")

target_precompile_headers(VulkanModule REUSE_FROM BECoreModule)

if(MSVC)
    target_compile_options(VulkanModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(VulkanModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(VulkanModule PRIVATE -include "BECore/pch.h")
endif()

target_include_directories(VulkanModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${CMAKE_CURRENT_SOURCE_DIR}/src/CoreVulkan
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    # SDL headers needed for SDL_Window* in device/swapchain
    ${PROJECT_ROOT}/external/SDL/include
    ${CMAKE_BINARY_DIR}
)

target_link_libraries(VulkanModule PUBLIC
    BECoreModule
    EventsModule
    SDLModule
    Vulkan::Headers
)

# Link against the vendored Vulkan loader stored in external/Vulkan-Loader/.
# Place vulkan-1.lib + vulkan-1.dll (Windows) or libvulkan.1.dylib (macOS)
# in the appropriate subfolder -- see external/Vulkan-Loader/README.md.
if(WIN32)
    set(_VULKAN_LIB "${VULKAN_LOADER_ROOT}/Windows/Lib/vulkan-1.lib")
    if(NOT EXISTS "${_VULKAN_LIB}")
        message(FATAL_ERROR
            "Vendored Vulkan loader not found: ${_VULKAN_LIB}\n"
            "See external/Vulkan-Loader/README.md for setup instructions.")
    endif()
    target_link_directories(VulkanModule PUBLIC "${VULKAN_LOADER_ROOT}/Windows/Lib")
    target_link_libraries(VulkanModule PUBLIC vulkan-1)
elseif(APPLE)
    set(_VULKAN_DYLIB "${VULKAN_LOADER_ROOT}/macOS/lib/libvulkan.1.dylib")
    if(NOT EXISTS "${_VULKAN_DYLIB}")
        message(FATAL_ERROR
            "Vendored Vulkan loader not found: ${_VULKAN_DYLIB}\n"
            "See external/Vulkan-Loader/README.md for setup instructions.")
    endif()
    target_link_directories(VulkanModule PUBLIC "${VULKAN_LOADER_ROOT}/macOS/lib")
    target_link_libraries(VulkanModule PUBLIC vulkan)
else()
    # Linux and other platforms: fall back to the system Vulkan loader.
    target_link_libraries(VulkanModule PUBLIC vulkan)
endif()
