# Vulkan Renderer Module
# Provides VulkanRendererBackend implementing IRenderer
#
# Uses vendored Vulkan-Headers (external/Vulkan-Headers) for <vulkan/vulkan.h>.
# Links against the vendored Vulkan loader from external/Vulkan-Loader/.
# See external/Vulkan-Loader/README.md for instructions on populating the binaries.
#
# Shaders (shaders/*.glsl) are compiled to SPIR-V by glslc (from VULKAN_SDK) and
# then embedded as constexpr uint32_t[] headers via cmake/SpvToHeader.cmake.
# Generated headers land in ${CMAKE_CURRENT_BINARY_DIR}/shaders/.

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

# Generated SPIR-V headers (rect_vert_spv.h, rect_frag_spv.h) land here.
target_include_directories(VulkanModule PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/shaders")

target_link_libraries(VulkanModule PUBLIC
    BECoreModule
    EventsModule
    SDLModule
    Vulkan::Headers
)

# Link against the Vulkan loader: prefer VULKAN_SDK (e.g. C:/VulkanSDK/1.4.341.1) when set and present, else vendored external/Vulkan-Loader/.
if(WIN32)
    set(_VULKAN_LIB_DIR "")
    if(VULKAN_SDK AND EXISTS "${VULKAN_SDK}/Lib/vulkan-1.lib")
        set(_VULKAN_LIB_DIR "${VULKAN_SDK}/Lib")
    elseif(EXISTS "${VULKAN_LOADER_ROOT}/Windows/Lib/vulkan-1.lib")
        set(_VULKAN_LIB_DIR "${VULKAN_LOADER_ROOT}/Windows/Lib")
    endif()
    if(NOT _VULKAN_LIB_DIR)
        message(FATAL_ERROR
            "Vulkan loader not found. Set VULKAN_SDK (e.g. C:/VulkanSDK/1.4.341.1) or place vulkan-1.lib in external/Vulkan-Loader/Windows/Lib.\n"
            "See external/Vulkan-Loader/README.md for setup instructions.")
    endif()
    target_link_directories(VulkanModule PUBLIC "${_VULKAN_LIB_DIR}")
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

# =============================================================================
# Shader compilation: GLSL -> SPIR-V -> embedded C++ header
# =============================================================================

# Locate glslc: prefer VULKAN_SDK bin directory, then fall back to system PATH.
set(_GLSLC "")
if(WIN32 AND VULKAN_SDK AND EXISTS "${VULKAN_SDK}/Bin/glslc.exe")
    set(_GLSLC "${VULKAN_SDK}/Bin/glslc.exe")
elseif(UNIX AND VULKAN_SDK AND EXISTS "${VULKAN_SDK}/bin/glslc")
    set(_GLSLC "${VULKAN_SDK}/bin/glslc")
else()
    find_program(_GLSLC glslc)
endif()

if(NOT _GLSLC)
    message(FATAL_ERROR
        "glslc not found. Set VULKAN_SDK to your Vulkan SDK installation "
        "(e.g. C:/VulkanSDK/1.4.341.1) or ensure glslc is on the system PATH.")
endif()
message(STATUS "VulkanModule: using glslc at '${_GLSLC}'")

set(_SHADER_SRC_DIR  "${CMAKE_CURRENT_SOURCE_DIR}/shaders")
set(_SHADER_BIN_DIR  "${CMAKE_CURRENT_BINARY_DIR}/shaders")
set(_SPV_TO_HEADER   "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpvToHeader.cmake")

file(MAKE_DIRECTORY "${_SHADER_BIN_DIR}")

# Helper macro: GLSL -> SPV -> C++ header
# Arguments: STAGE (vert|frag), BASE_NAME (e.g. rect), VAR_NAME (e.g. kRectVertSpv)
macro(compile_shader STAGE BASE_NAME VAR_NAME)
    set(_GLSL_FILE   "${_SHADER_SRC_DIR}/${BASE_NAME}.${STAGE}.glsl")
    set(_SPV_FILE    "${_SHADER_BIN_DIR}/${BASE_NAME}_${STAGE}.spv")
    set(_HEADER_FILE "${_SHADER_BIN_DIR}/${BASE_NAME}_${STAGE}_spv.h")

    add_custom_command(
        OUTPUT "${_SPV_FILE}"
        COMMAND "${_GLSLC}" -fshader-stage=${STAGE} "${_GLSL_FILE}" -o "${_SPV_FILE}"
        DEPENDS "${_GLSL_FILE}"
        COMMENT "Compiling ${BASE_NAME}.${STAGE}.glsl -> ${BASE_NAME}_${STAGE}.spv"
        VERBATIM
    )

    add_custom_command(
        OUTPUT "${_HEADER_FILE}"
        COMMAND "${CMAKE_COMMAND}"
            "-DINPUT=${_SPV_FILE}"
            "-DOUTPUT=${_HEADER_FILE}"
            "-DVAR_NAME=${VAR_NAME}"
            -P "${_SPV_TO_HEADER}"
        DEPENDS "${_SPV_FILE}" "${_SPV_TO_HEADER}"
        COMMENT "Generating ${BASE_NAME}_${STAGE}_spv.h"
        VERBATIM
    )
endmacro()

compile_shader(vert rect kRectVertSpv)
compile_shader(frag rect kRectFragSpv)

add_custom_target(VulkanShaders
    DEPENDS
        "${_SHADER_BIN_DIR}/rect_vert_spv.h"
        "${_SHADER_BIN_DIR}/rect_frag_spv.h"
)
set_target_properties(VulkanShaders PROPERTIES FOLDER "Modules")
add_dependencies(VulkanModule VulkanShaders)
