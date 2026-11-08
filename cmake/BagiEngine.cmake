# =============================================================================
# BagiEngine CMake entry point
# =============================================================================
# A top-level project (the engine repository itself, or a game that carries the engine as a
# submodule) includes this file right after project():
#
#   include(<path-to-engine>/cmake/BagiEngine.cmake)
#
# It is include()d, not add_subdirectory()'d, on purpose: the compile definitions, exception flags
# and output directories set here must apply to the game's targets as well.
#
# Public API (defined below): be_add_module, be_add_game_reflection, be_add_game_executable,
# be_add_game_tests, be_add_game_control_tests, be_link_engine, be_force_include_pch.
# =============================================================================
include_guard(GLOBAL)

# Root of the engine checkout (this file lives in <engine>/cmake)
get_filename_component(BAGI_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# =============================================================================
# C++ Standard Configuration
# =============================================================================
# Set C++ standard FIRST, before adding any subdirectories
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =============================================================================
# External Dependencies
# =============================================================================
# Build external libraries directly instead of using a separate SDK build

# EA Libraries (Vendored EABase + EASTL)
message(STATUS "Configuring EA Libraries (vendored)...")
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/EABase)

# SDL3 - Shared library
message(STATUS "Configuring SDL3...")
set(SDL_SHARED ON CACHE BOOL "Build SDL3 as shared library" FORCE)
set(SDL_STATIC OFF CACHE BOOL "Build SDL3 as static library" FORCE)
set(SDL_TEST OFF CACHE BOOL "Build SDL3 tests" FORCE)
set(SDL_TESTS OFF CACHE BOOL "Build SDL3 tests" FORCE)
add_subdirectory(${BAGI_ENGINE_DIR}/external/SDL)

# SDL3_image
message(STATUS "Configuring SDL3_image...")
set(SDLIMAGE_AVIF OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_VENDORED ON CACHE BOOL "" FORCE)
set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(${BAGI_ENGINE_DIR}/external/SDL_image)

# Vulkan-Headers (vendored, include-only - provides Vulkan::Headers target)
message(STATUS "Configuring Vulkan-Headers (vendored)...")
add_subdirectory(${BAGI_ENGINE_DIR}/external/Vulkan-Headers)

# Vendored libraries (EnTT, pugixml, ImGui, fmt, nlohmann/json, stb)
message(STATUS "Configuring vendored libraries...")
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/EnTT)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/pugixml)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/imgui)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/fmt)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/nlohmann)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/stb)

# The vendored imgui CMakeLists points at ${CMAKE_SOURCE_DIR}/external/SDL/include, which is only right when
# the engine is the top-level project. Vendored code is not edited, so add the correct path here.
target_include_directories(imgui PRIVATE ${BAGI_ENGINE_DIR}/external/SDL/include)

# Group external libraries in IDE
set_target_properties(SDL3-shared PROPERTIES FOLDER "External")
set_target_properties(fmt PROPERTIES FOLDER "External")

# Export compile commands for IntelliSense and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set_property(GLOBAL PROPERTY USE_FOLDERS ON)

# =============================================================================
# Compiler Detection and Configuration
# =============================================================================
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
    add_compile_definitions(COMPILER_CLANG)
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Compiler: GCC ${CMAKE_CXX_COMPILER_VERSION}")
    add_compile_definitions(COMPILER_GCC)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    message(STATUS "Compiler: MSVC ${CMAKE_CXX_COMPILER_VERSION}")
    add_compile_definitions(COMPILER_MSVC)
else()
    message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
endif()

# =============================================================================
# Exception Handling Configuration
# =============================================================================
option(ENGINE_NO_EXCEPTIONS "Disable C++ exceptions" ON)

if(ENGINE_NO_EXCEPTIONS)
    message(STATUS "C++ exceptions: DISABLED")
    add_compile_definitions(ENGINE_NO_EXCEPTIONS=1)
    add_compile_definitions(EASTL_EXCEPTIONS_ENABLED=0)

    if(NOT MSVC)
        add_compile_options(-fno-exceptions)
    else()
        # MSVC: /EHs-c- отключает исключения
        string(REPLACE "/EHsc" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        add_compile_options(/EHs-c-)
    endif()
else()
    message(STATUS "C++ exceptions: ENABLED")
endif()

# =============================================================================
# Tests (GoogleTest)
# =============================================================================
# Added after the exception options above on purpose: gtest then inherits the same
# -fno-exceptions / ENGINE_NO_EXCEPTIONS as the test translation units.
option(ENGINE_BUILD_TESTS "Build the gtest executables (BagiEngineTests and the game test executables)" ON)

if(ENGINE_BUILD_TESTS)
    if(NOT EXISTS "${BAGI_ENGINE_DIR}/external/googletest/CMakeLists.txt")
        message(FATAL_ERROR "GoogleTest submodule is missing. Run: git submodule update --init external/googletest "
                            "(or configure with -DENGINE_BUILD_TESTS=OFF to skip the tests).")
    endif()
    message(STATUS "Configuring GoogleTest...")
    enable_testing()
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    # MSVC: gtest defaults to the static CRT (/MT) while the engine uses /MD
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    add_subdirectory(${BAGI_ENGINE_DIR}/external/googletest)
    set_target_properties(gtest PROPERTIES FOLDER "External")
endif()

# =============================================================================
# RmlUi (vendored, optional)
# =============================================================================
# Added after the exception options above on purpose, like GoogleTest. It is the third candidate for how UI screens
# are authored (src/Modules/BECore/UI/AuthoringDecision.md); with the option ON the RmlUiHost module (its host, below) is built
# and linked into every executable.
option(ENGINE_UI_RMLUI "Build the vendored RmlUi and its host module RmlUiHost (a candidate for authoring UI screens)" OFF)

if(ENGINE_UI_RMLUI)
    message(STATUS "Configuring RmlUi (vendored)...")
    add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/RmlUi)
endif()

# =============================================================================
# Platform Detection
# =============================================================================
if(WIN32)
    add_compile_definitions(PLATFORM_WINDOWS)
    message(STATUS "Platform: Windows")
elseif(APPLE)
    add_compile_definitions(PLATFORM_MACOS)
    message(STATUS "Platform: macOS")
else()
    add_compile_definitions(PLATFORM_LINUX)
    message(STATUS "Platform: Linux")
endif()

# =============================================================================
# Output Directories Configuration
# =============================================================================
# Формируем имя output директории: {Compiler}{Platform}
# CMake сам создаёт подпапки Debug/Release внутри
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    set(COMPILER_NAME "Clang")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(COMPILER_NAME "GCC")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(COMPILER_NAME "MSVC")
else()
    set(COMPILER_NAME "Unknown")
endif()

if(WIN32)
    set(PLATFORM_NAME "Windows")
elseif(APPLE)
    set(PLATFORM_NAME "macOS")
else()
    set(PLATFORM_NAME "Linux")
endif()

set(OUTPUT_DIR_NAME "${COMPILER_NAME}${PLATFORM_NAME}")
# Relative to the top-level project, so a game that carries the engine builds into its own build/
set(OUTPUT_BASE_DIR "${CMAKE_SOURCE_DIR}/build/${OUTPUT_DIR_NAME}")

# Executables (.exe)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}")
# Shared libraries (.dll on Windows, .so on Linux)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}")
# Static libraries (.lib) - keep in output directory
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${OUTPUT_BASE_DIR}/lib")

message(STATUS "Output directory: ${OUTPUT_BASE_DIR}")

# =============================================================================
# Assert System Options
# =============================================================================
option(ENGINE_ENABLE_ASSERTS "Enable ASSERT macros" ON)
option(ENGINE_ENABLE_EXPECTS "Enable EXPECT macros" ON)

# In Release builds, disable asserts by default (can be overridden)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    if(NOT DEFINED CACHE{ENGINE_ENABLE_ASSERTS})
        set(ENGINE_ENABLE_ASSERTS OFF CACHE BOOL "Enable ASSERT macros" FORCE)
    endif()
    if(NOT DEFINED CACHE{ENGINE_ENABLE_EXPECTS})
        set(ENGINE_ENABLE_EXPECTS OFF CACHE BOOL "Enable EXPECT macros" FORCE)
    endif()
endif()

# Pass defines to code
if(ENGINE_ENABLE_ASSERTS)
    add_compile_definitions(ENGINE_ENABLE_ASSERTS=1)
    message(STATUS "Asserts: ENABLED")
else()
    add_compile_definitions(ENGINE_ENABLE_ASSERTS=0)
    message(STATUS "Asserts: DISABLED")
endif()

if(ENGINE_ENABLE_EXPECTS)
    add_compile_definitions(ENGINE_ENABLE_EXPECTS=1)
    message(STATUS "Expects: ENABLED")
else()
    add_compile_definitions(ENGINE_ENABLE_EXPECTS=0)
    message(STATUS "Expects: DISABLED")
endif()

# =============================================================================
# Editor Option
# =============================================================================
# BE_WITH_EDITOR decides whether the build contains the editor: the panels under src/Widgets
# (ConfigEditor, EditorLayout, SceneEditor, TextureEditor, Viewport) and the tests of those panels.
#   AUTO (default): ON, except in a single-config build of type Release, RelWithDebInfo or MinSizeRel, where it is OFF.
#                   So a Debug build has the editor and a Release build does not.
#   ON:             the editor is built (also in Release).
#   OFF:            the editor is not built. A Debug build ignores it (WARNING, the editor is built anyway):
#                   Debug builds always include the editor.
# A multi-config generator (Visual Studio, Ninja Multi-Config) picks the configuration at build time, but the sources
# are picked now, so there AUTO means ON; use -DBE_WITH_EDITOR=OFF for a build without the editor.
# The result is the variable BE_EDITOR_ENABLED (used by src/CMakeLists.txt and tests/CMakeLists.txt) and the
# compile definition BE_WITH_EDITOR=1|0 on every target. ImGui and ImGuiWidget stay in every build.
set(BE_WITH_EDITOR "AUTO" CACHE STRING "Build the editor panels: AUTO (ON, except OFF in a single-config Release/RelWithDebInfo/MinSizeRel build), ON or OFF. Debug always has the editor")
set_property(CACHE BE_WITH_EDITOR PROPERTY STRINGS AUTO ON OFF)

string(TOUPPER "${BE_WITH_EDITOR}" BE_WITH_EDITOR_VALUE)
get_property(BE_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(BE_WITH_EDITOR_VALUE STREQUAL "ON")
    set(BE_EDITOR_ENABLED TRUE)
elseif(BE_WITH_EDITOR_VALUE STREQUAL "OFF")
    set(BE_EDITOR_ENABLED FALSE)
    if(NOT BE_IS_MULTI_CONFIG AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "BE_WITH_EDITOR=OFF is ignored in a Debug build: Debug builds always include the editor")
        set(BE_EDITOR_ENABLED TRUE)
    endif()
elseif(BE_WITH_EDITOR_VALUE STREQUAL "AUTO")
    set(BE_EDITOR_ENABLED TRUE)
    if(NOT BE_IS_MULTI_CONFIG AND CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
        set(BE_EDITOR_ENABLED FALSE)
    endif()
else()
    message(FATAL_ERROR "BE_WITH_EDITOR is '${BE_WITH_EDITOR}'; it must be AUTO, ON or OFF")
endif()

if(BE_EDITOR_ENABLED)
    add_compile_definitions(BE_WITH_EDITOR=1)
    message(STATUS "Editor: ENABLED (BE_WITH_EDITOR=${BE_WITH_EDITOR_VALUE})")
else()
    add_compile_definitions(BE_WITH_EDITOR=0)
    message(STATUS "Editor: DISABLED (BE_WITH_EDITOR=${BE_WITH_EDITOR_VALUE})")
endif()

# =============================================================================
# Python Virtual Environment Detection
# =============================================================================
# Use Python from .venv/ if it exists, otherwise system Python
# This ensures all dependencies (clang, jinja2) are available

if(WIN32)
    set(VENV_PYTHON "${BAGI_ENGINE_DIR}/.venv/Scripts/python.exe")
else()
    set(VENV_PYTHON "${BAGI_ENGINE_DIR}/.venv/bin/python3")
endif()

if(EXISTS ${VENV_PYTHON})
    set(Python3_EXECUTABLE ${VENV_PYTHON} CACHE FILEPATH "Python interpreter from venv" FORCE)
    message(STATUS "Using Python from venv: ${VENV_PYTHON}")
else()
    message(STATUS "Venv not found at ${VENV_PYTHON}, using system Python")
    message(STATUS "  Hint: Run 'CI/setup_venv.bat' (Windows) or 'CI/setup_venv.sh' to create venv")
endif()

# =============================================================================
# Build helpers shared by the engine modules, games and tests
# =============================================================================

# be_force_include_pch(<target>)
# Force-includes BECore/pch.h so sources can rely on it without including it themselves. The target
# must also reuse the PCH: target_precompile_headers(<target> REUSE_FROM BECoreModule).
function(be_force_include_pch TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /FI"BECore/pch.h")
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -include "BECore/pch.h")
    endif()
endfunction()

# be_add_module(<Name> [LINK <libs>...])
# STATIC library <Name>Module from <current dir>/src/**.{cpp,h}. Include root is <current dir>/src, so
# src/<Name>/Foo.h is reached as <Name>/Foo.h. Reuses the PCH of BECoreModule.
function(be_add_module NAME)
    cmake_parse_arguments(ARG "" "" "LINK" ${ARGN})
    set(TARGET_NAME ${NAME}Module)

    file(GLOB_RECURSE MODULE_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    )

    add_library(${TARGET_NAME} STATIC ${MODULE_SOURCES})
    source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "${NAME}" FILES ${MODULE_SOURCES})
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Modules")

    target_precompile_headers(${TARGET_NAME} REUSE_FROM BECoreModule)
    be_force_include_pch(${TARGET_NAME})

    target_include_directories(${TARGET_NAME} PUBLIC
        ${BAGI_ENGINE_DIR}/src
        ${BAGI_ENGINE_DIR}/src/Modules
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
    )

    # IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
    target_compile_definitions(${TARGET_NAME} PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)

    target_link_libraries(${TARGET_NAME} PUBLIC ${ARG_LINK})
endfunction()

# be_link_engine(<target> [WITH_RUNTIME])
# Links the engine core with WHOLE_ARCHIVE: registration code (AbstractFactory) lives in static
# initializers, which the linker would drop from a plain static library. WITH_RUNTIME adds the pieces
# an executable needs on top: renderer backends, game infrastructure and the application/editor runtime.
function(be_link_engine TARGET_NAME)
    cmake_parse_arguments(ARG "WITH_RUNTIME" "" "" ${ARGN})

    target_link_libraries(${TARGET_NAME} PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,BECoreModule>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,EventsModule>
        $<LINK_LIBRARY:WHOLE_ARCHIVE,TaskSystemModule>
    )

    # The component of the RmlUi host registers in a static initializer too
    if(TARGET RmlUiHostModule)
        target_link_libraries(${TARGET_NAME} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,RmlUiHostModule>)
    endif()

    if(ARG_WITH_RUNTIME)
        target_link_libraries(${TARGET_NAME} PRIVATE
            $<LINK_LIBRARY:WHOLE_ARCHIVE,SDLModule>
            $<LINK_LIBRARY:WHOLE_ARCHIVE,VulkanModule>
            $<LINK_LIBRARY:WHOLE_ARCHIVE,GamesModule>
            $<LINK_LIBRARY:WHOLE_ARCHIVE,RuntimeModule>
        )
    endif()
endfunction()

# Runtime DLLs/dylibs next to an executable, platform linker settings.
# be_deploy_runtime_libs(<target>)
function(be_deploy_runtime_libs TARGET_NAME)
    if(WIN32)
        foreach(_sdl_target SDL3-shared SDL3_image-shared)
            if(TARGET ${_sdl_target})
                add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    $<TARGET_FILE:${_sdl_target}>
                    $<TARGET_FILE_DIR:${TARGET_NAME}>
                )
            endif()
        endforeach()
    endif()
endfunction()

# =============================================================================
# Reflection System
# =============================================================================
# Find Python for the reflection code generator
find_package(Python3 COMPONENTS Interpreter)

# Engine target whose reflected classes are the base for every game (factory bases such as IComponent, IGame)
set(BAGI_ENGINE_REFLECTION_TARGET BECoreModule)

if(Python3_FOUND)
    message(STATUS "Python3 found: ${Python3_EXECUTABLE}")

    # Directory for generated reflection files
    # Use CMAKE_BINARY_DIR so generated files go into the actual build directory
    # (e.g., build/ClangWindowsSolution/Generated instead of build/ClangWindows/Generated)
    # Use "Generated" with capital G so include path is <Generated/File.gen.hpp>
    set(REFLECTION_GENERATED_DIR "${CMAKE_BINARY_DIR}/Generated" CACHE PATH "Directory for generated reflection files")
    file(MAKE_DIRECTORY ${REFLECTION_GENERATED_DIR})

    # Meta-Generator script path (regex-based, no external dependencies)
    set(META_GENERATOR_SCRIPT "${BAGI_ENGINE_DIR}/CI/meta_generator/meta_generator.py")

    # Runs meta_generator.py for TARGET_NAME with the given extra arguments, then adds the .gen.hpp files
    # listed in the generator's manifest to the target. Only the files this target owns are added: the
    # engine and every game share REFLECTION_GENERATED_DIR, so globbing it would mix them up.
    function(_be_run_meta_generator TARGET_NAME)
        if(NOT EXISTS ${META_GENERATOR_SCRIPT})
            message(FATAL_ERROR "Meta-generator script not found: ${META_GENERATOR_SCRIPT}")
        endif()

        message(STATUS "Running meta_generator for ${TARGET_NAME}...")

        # One cache per target: a game's cache also holds the parsed engine headers
        set(META_CACHE_DIR "${CMAKE_BINARY_DIR}/.meta_cache/${TARGET_NAME}")
        set(META_MANIFEST "${META_CACHE_DIR}/generated_files.txt")
        file(MAKE_DIRECTORY "${META_CACHE_DIR}")

        execute_process(
            COMMAND ${Python3_EXECUTABLE} -u ${META_GENERATOR_SCRIPT}
                ${ARGN}
                --output-dir "${REFLECTION_GENERATED_DIR}"
                --cache-dir "${META_CACHE_DIR}"
                --manifest "${META_MANIFEST}"
            RESULT_VARIABLE META_GEN_RESULT
            WORKING_DIRECTORY ${BAGI_ENGINE_DIR}
        )

        if(NOT META_GEN_RESULT EQUAL 0)
            message(FATAL_ERROR "Meta-generator failed for ${TARGET_NAME}:\n"
                "  Exit code: ${META_GEN_RESULT}\n"
                "  See output above for details."
            )
        endif()

        file(STRINGS "${META_MANIFEST}" GENERATED_FILES)

        if(GENERATED_FILES)
            set_source_files_properties(${GENERATED_FILES} PROPERTIES GENERATED TRUE)
            target_sources(${TARGET_NAME} PRIVATE ${GENERATED_FILES})
            list(LENGTH GENERATED_FILES GEN_COUNT)
            message(STATUS "Reflection enabled for ${TARGET_NAME}: ${GEN_COUNT} files generated")
        endif()

        # Add CMAKE_BINARY_DIR to include paths so <Generated/File.gen.hpp> works
        target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_BINARY_DIR})

        # Define REFLECTION_GENERATED so code can detect generated files are available
        target_compile_definitions(${TARGET_NAME} PRIVATE REFLECTION_GENERATED=1)
    endfunction()

    # add_reflection_target(<Target>)
    # Engine-side reflection. Dirs are read from CI/meta_generator/reflection_targets.json for <Target>.
    # Do not use it for game modules (see be_add_game_reflection): the file lists engine directories only.
    #
    # This will:
    #   1. Run meta_generator.py with caching (only re-parses changed files)
    #   2. Generate .gen.hpp files in ${REFLECTION_GENERATED_DIR}
    #   3. Add the generated files of this target to its sources
    function(add_reflection_target TARGET_NAME)
        _be_run_meta_generator(${TARGET_NAME} --target ${TARGET_NAME} --project-root ${BAGI_ENGINE_DIR})
    endfunction()

    # be_add_game_reflection(<Target> [SOURCE_DIRS <dirs>...] [INCLUDE_DIRS <dirs>...])
    # Reflection for a game module. Scans SOURCE_DIRS (default <current dir>/src) for BE_CLASS / BE_REFLECT_FIELD;
    # the engine's reflected headers are parsed as reference only, so factory bases such as IComponent or IGame
    # are known and the game's classes register with them. Generated files go to the shared Generated/ dir.
    # INCLUDE_DIRS (default <current dir>/src) are used to compute the #include path of the game's headers.
    function(be_add_game_reflection TARGET_NAME)
        cmake_parse_arguments(ARG "" "" "SOURCE_DIRS;INCLUDE_DIRS" ${ARGN})
        if(NOT ARG_SOURCE_DIRS)
            set(ARG_SOURCE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)
        endif()
        if(NOT ARG_INCLUDE_DIRS)
            set(ARG_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/src)
        endif()

        set(META_ARGS "")
        foreach(DIR ${ARG_SOURCE_DIRS})
            list(APPEND META_ARGS --source-dir ${DIR})
        endforeach()
        foreach(DIR ${ARG_INCLUDE_DIRS})
            list(APPEND META_ARGS --include-dir ${DIR})
        endforeach()
        list(APPEND META_ARGS --reference-target ${BAGI_ENGINE_REFLECTION_TARGET} --project-root ${BAGI_ENGINE_DIR})

        _be_run_meta_generator(${TARGET_NAME} ${META_ARGS})
    endfunction()
else()
    message(WARNING "Python3 not found - reflection code generation will be disabled")

    # Stub functions when Python is not available
    function(add_reflection_target TARGET_NAME)
        message(WARNING "add_reflection_target: Skipped (Python3 not found)")
    endfunction()

    function(be_add_game_reflection TARGET_NAME)
        message(WARNING "be_add_game_reflection: Skipped (Python3 not found)")
    endfunction()
endif()

# =============================================================================
# Engine modules
# =============================================================================
# BECoreModule должен быть объявлен первым, так как другие модули используют его PCH
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/BECore)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Events)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/TaskSystem)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/RemoteControl)
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/SDL)

# Optional: path to Vulkan SDK (Windows). If set and Lib/vulkan-1.lib exists, used for linking; else vendored external/Vulkan-Loader is used.
set(VULKAN_SDK "C:/VulkanSDK/1.4.341.1" CACHE PATH "Path to Vulkan SDK (optional; used for loader on Windows)")
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Vulkan)

# Game infrastructure (IGame, GameManager). Concrete games live outside the engine and register through it.
add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/Games)

# The host of the vendored RmlUi: the interfaces over the engine and the scene component (only with ENGINE_UI_RMLUI)
if(ENGINE_UI_RMLUI)
    add_subdirectory(${BAGI_ENGINE_DIR}/src/Modules/RmlUiHost)
endif()

# Application loop, widgets and the editor UI. Linked into every game executable.
add_subdirectory(${BAGI_ENGINE_DIR}/src)

# =============================================================================
# Game executables
# =============================================================================
# Debugger visualizers (.natvis) from tools/natvis/ (EASTL, etc.)
set(BAGI_NATVIS_FILES "")
if(MSVC)
    file(GLOB BAGI_NATVIS_FILES "${BAGI_ENGINE_DIR}/tools/natvis/*.natvis")
    if(BAGI_NATVIS_FILES)
        # Copy to user Visualizers folder so natvis works with "Open Folder" and any generator
        option(ENGINE_INSTALL_NATVIS "Copy .natvis to Visual Studio Visualizers folder (recommended)" ON)
        if(ENGINE_INSTALL_NATVIS AND WIN32)
            set(_vis_dirs "")
            if(DEFINED ENV{USERPROFILE})
                list(APPEND _vis_dirs "$ENV{USERPROFILE}/Documents/Visual Studio 2022/Visualizers")
                list(APPEND _vis_dirs "$ENV{USERPROFILE}/Documents/Visual Studio 2019/Visualizers")
            endif()
            foreach(_dir ${_vis_dirs})
                if(EXISTS "${_dir}")
                    file(COPY ${BAGI_NATVIS_FILES} DESTINATION "${_dir}")
                    message(STATUS "Natvis: copied to ${_dir}")
                    break()
                endif()
            endforeach()
            unset(_vis_dirs)
        endif()
    endif()
endif()

# Vendored Vulkan loader (macOS dylib, Windows dll)
set(VULKAN_LOADER_ROOT "${BAGI_ENGINE_DIR}/external/Vulkan-Loader")

# be_add_game_executable(<Name> [GAME_ROOT <dir>] [SOURCES <files>...] [MODULES <targets>...])
# Executable for one game: the engine's main.cpp, the engine runtime (WHOLE_ARCHIVE) and the game's modules
# (WHOLE_ARCHIVE too, otherwise their IGame/IComponent registrations are dropped by the linker).
# GAME_ROOT (default: current dir) is the game's content root with config/ and assets/ on top of the engine's.
function(be_add_game_executable NAME)
    cmake_parse_arguments(ARG "" "GAME_ROOT" "SOURCES;MODULES" ${ARGN})
    if(NOT ARG_GAME_ROOT)
        set(ARG_GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    set(ENGINE_MAIN ${BAGI_ENGINE_DIR}/src/main.cpp)

    # For Windows: create WIN32 executable (no console window)
    if(WIN32)
        add_executable(${NAME} WIN32 ${ENGINE_MAIN} ${ARG_SOURCES})
    else()
        add_executable(${NAME} ${ENGINE_MAIN} ${ARG_SOURCES})
    endif()

    target_include_directories(${NAME} PRIVATE ${BAGI_ENGINE_DIR}/src ${CMAKE_BINARY_DIR})

    # REUSE_FROM reuses the PCH of BECoreModule without recompiling it
    target_precompile_headers(${NAME} REUSE_FROM BECoreModule)
    be_force_include_pch(${NAME})

    be_link_engine(${NAME} WITH_RUNTIME)
    foreach(MODULE ${ARG_MODULES})
        target_link_libraries(${NAME} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,${MODULE}>)
    endforeach()

    target_compile_definitions(${NAME} PRIVATE
        IMGUI_VULKAN_AVAILABLE=1
        REFLECTION_GENERATED=1
        "BAGI_ENGINE_ROOT=\"${BAGI_ENGINE_DIR}\""
        "BAGI_GAME_ROOT=\"${ARG_GAME_ROOT}\""
    )

    be_deploy_runtime_libs(${NAME})

    # Vulkan Loader (vulkan-1.dll / libvulkan.1.dylib) next to the executable: VULKAN_SDK/Bin first, else the vendored folder
    if(WIN32)
        set(_VULKAN_DLL "")
        if(VULKAN_SDK AND EXISTS "${VULKAN_SDK}/Bin/vulkan-1.dll")
            set(_VULKAN_DLL "${VULKAN_SDK}/Bin/vulkan-1.dll")
        elseif(EXISTS "${VULKAN_LOADER_ROOT}/Windows/Bin/vulkan-1.dll")
            set(_VULKAN_DLL "${VULKAN_LOADER_ROOT}/Windows/Bin/vulkan-1.dll")
        endif()
        if(_VULKAN_DLL)
            add_custom_command(TARGET ${NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_VULKAN_DLL}"
                $<TARGET_FILE_DIR:${NAME}>
            )
        endif()
    elseif(APPLE)
        set(_VULKAN_RUNTIME "${VULKAN_LOADER_ROOT}/macOS/lib/libvulkan.1.dylib")
        if(EXISTS "${_VULKAN_RUNTIME}")
            add_custom_command(TARGET ${NAME} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_VULKAN_RUNTIME}"
                $<TARGET_FILE_DIR:${NAME}>
            )
        endif()
        set_target_properties(${NAME} PROPERTIES
            BUILD_RPATH "@executable_path"
            INSTALL_RPATH "@executable_path"
        )
    endif()

    if(MSVC AND BAGI_NATVIS_FILES)
        set_property(TARGET ${NAME} PROPERTY VS_DEBUGGER_VISUALIZER_FILE "${BAGI_NATVIS_FILES}")
    endif()
endfunction()

# =============================================================================
# Game tests
# =============================================================================
if(ENGINE_BUILD_TESTS)
    include(GoogleTest)

    # BagiEngineTestSupport: main(), the engine environment and the assert bridge shared by every gtest executable
    add_subdirectory(${BAGI_ENGINE_DIR}/tests/Common)

    # be_add_game_tests(<Name> SOURCES <files>... [LINK <targets>...])
    # gtest executable on top of BagiEngineTestSupport, registered with ctest (one process per test, so the
    # global engine state cannot leak between tests). Tests run in the default working directory, which is
    # inside the repository: FileSystem finds config/ by walking up from it. Do not pass WORKING_DIRECTORY
    # to gtest_discover_tests: it also decides where cmake_test_discovery_*.json is written.
    # The test presets run ctest in parallel (execution.jobs of test-base in CMakePresets.json), so a test case
    # runs beside others, in other processes: it may share read-only inputs (config/, the
    # source tree) and nothing it writes. Temp folders and files carry the test name and a per-process tag
    # (tests/Common/TempPath.h), servers bind port 0, nothing is awaited by a fixed sleep, and no test reads the
    # engine.log of the working directory (every process truncates it). A test that cannot keep to this gets
    # RUN_SERIAL (alone in the run) or a RESOURCE_LOCK (not beside the tests with the same lock) in
    # set_tests_properties; none of the engine's gtest cases needs either.
    function(be_add_game_tests NAME)
        cmake_parse_arguments(ARG "" "" "SOURCES;LINK;WHOLE_ARCHIVE" ${ARGN})

        add_executable(${NAME} ${ARG_SOURCES})
        source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "Tests" FILES ${ARG_SOURCES})
        set_target_properties(${NAME} PROPERTIES FOLDER "Tests")

        # Reuse the PCH of BECoreModule; test sources include engine headers that rely on it
        target_precompile_headers(${NAME} REUSE_FROM BECoreModule)
        be_force_include_pch(${NAME})

        # IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected.
        # REFLECTION_GENERATED enables the generated reflection code (<Generated/*.gen.hpp>).
        target_compile_definitions(${NAME} PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)

        # ${CMAKE_BINARY_DIR} for <Generated/*.gen.hpp>, the calling directory for <Folder/...Test.h>
        target_include_directories(${NAME} PRIVATE ${CMAKE_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

        be_link_engine(${NAME})
        target_link_libraries(${NAME} PRIVATE BagiEngineTestSupport SDLModule GamesModule ${ARG_LINK})
        # Modules whose components must be creatable by name from a scene file (the registration is a static initializer that a plain link drops
        # when no test names the class), the way be_add_game_executable links the game's modules
        foreach(MODULE ${ARG_WHOLE_ARCHIVE})
            target_link_libraries(${NAME} PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,${MODULE}>)
        endforeach()

        if(APPLE)
            # libSDL3 is written next to the executables (CMAKE_LIBRARY_OUTPUT_DIRECTORY)
            set_target_properties(${NAME} PROPERTIES
                BUILD_RPATH "@executable_path"
                INSTALL_RPATH "@executable_path"
            )
        endif()

        be_deploy_runtime_libs(${NAME})

        gtest_discover_tests(${NAME} DISCOVERY_MODE PRE_TEST)
    endfunction()

    # be_add_game_control_tests(<ExeTarget> [SCRIPT <file>])
    # ctest entry <ExeTarget>ControlSmoke: starts the game executable with the control API on a free port and plays a
    # short scenario over WebSocket, once on the real clock and once with --manual-clock (tools/control/tests/smoke.py,
    # or SCRIPT with the same arguments). Call it from the game's CMakeLists, not from the engine's tests/, so that no
    # engine CMake names a game. It needs Python 3 and does not build the executable (a missing one is a skip). The
    # script runs the game --headless (no window; a SCRIPT has to do the same if it does not want one). It exits with 77
    # for "skipped". The test environment has BAGI_ENGINE_DIR (the engine checkout), so that a game's SCRIPT can import
    # the helpers of tools/control/tests/smoke.py.
    # The smoke test runs beside the unit tests of the parallel ctest run: it takes a free port by itself (a port
    # that is taken in the moment between the probe and the bind makes smoke.py retry once on a new one) and
    # touches no file the unit tests use. The RESOURCE_LOCK only keeps two smoke tests of one ctest run apart;
    # it is not RUN_SERIAL, which would stop every other test while it runs. TIMEOUT 120 covers the two runs of
    # the script with their own limits (30 s startup, 15 s shutdown) and one retry on a loaded machine.
    function(be_add_game_control_tests EXE_TARGET)
        cmake_parse_arguments(ARG "" "SCRIPT" "" ${ARGN})
        if(NOT ARG_SCRIPT)
            set(ARG_SCRIPT ${BAGI_ENGINE_DIR}/tools/control/tests/smoke.py)
        endif()
        if(NOT Python3_FOUND)
            message(WARNING "be_add_game_control_tests(${EXE_TARGET}): Python3 not found, the control smoke test is not registered")
            return()
        endif()

        set(TEST_NAME ${EXE_TARGET}ControlSmoke)
        add_test(NAME ${TEST_NAME} COMMAND ${Python3_EXECUTABLE} ${ARG_SCRIPT} --exe $<TARGET_FILE:${EXE_TARGET}>)
        set_tests_properties(${TEST_NAME} PROPERTIES
            SKIP_RETURN_CODE 77
            TIMEOUT 120
            RESOURCE_LOCK bagi_control_smoke
            ENVIRONMENT "BAGI_ENGINE_DIR=${BAGI_ENGINE_DIR}"
        )
    endfunction()
endif()
