# =============================================================================
# Platform options: what the build contains besides the core (Vulkan, the socket server, SDL linkage, threads)
# =============================================================================
# Included once by cmake/BagiEngine.cmake after the C++ standard block and before the SDL block.
#
#   ENGINE_WITH_VULKAN          ON   VulkanModule: the Vulkan renderer backend and its loader library.
#   ENGINE_WITH_REMOTE_CONTROL  ON   RemoteControlModule (the WebSocket server of the control API) and its start in Application.
#   ENGINE_STATIC_SDL           OFF  SDL3 and SDL3_image as static libraries. The target name SDL3-shared stays (an INTERFACE
#                                    shim over SDL3::SDL3-static, made in cmake/BagiEngine.cmake), because the vendored imgui
#                                    CMake file and our modules link that name.
#   ENGINE_SINGLE_THREAD        OFF  The task system creates no thread (the definition BE_SINGLE_THREAD=1; TaskSystem/CLAUDE.md).
#
# The browser (Emscripten) build has no Vulkan, no sockets server, no shared libraries and no threads, so on EMSCRIPTEN all four
# are forced to those values. ENGINE_TARGET_WEB mirrors EMSCRIPTEN for the other cmake files. A desktop build can switch the
# options one by one to check the web configuration without an Emscripten SDK (root CLAUDE.md, "Build Commands").
include_guard(GLOBAL)

if(EMSCRIPTEN)
    set(ENGINE_TARGET_WEB TRUE)
else()
    set(ENGINE_TARGET_WEB FALSE)
endif()

option(ENGINE_WITH_VULKAN "Build the Vulkan renderer backend (VulkanModule)" ON)
option(ENGINE_WITH_REMOTE_CONTROL "Build the WebSocket server of the control API (RemoteControlModule)" ON)
option(ENGINE_STATIC_SDL "Build SDL3 and SDL3_image as static libraries" OFF)
option(ENGINE_SINGLE_THREAD "Run the task system without worker threads" OFF)

if(ENGINE_TARGET_WEB)
    set(ENGINE_WITH_VULKAN OFF CACHE BOOL "Build the Vulkan renderer backend (VulkanModule)" FORCE)
    set(ENGINE_WITH_REMOTE_CONTROL OFF CACHE BOOL "Build the WebSocket server of the control API (RemoteControlModule)" FORCE)
    set(ENGINE_STATIC_SDL ON CACHE BOOL "Build SDL3 and SDL3_image as static libraries" FORCE)
    set(ENGINE_SINGLE_THREAD ON CACHE BOOL "Run the task system without worker threads" FORCE)
    add_compile_definitions(PLATFORM_WEB)
    # EABase (vendored) knows no Emscripten: without help it stops with "Unknown platform / processor". Its only branch that takes the
    # platform from a definition and probes no compiler macro is EA_PLATFORM_SAMSUNG_TV (Linux, POSIX, little endian, ARM32). ARM32 is
    # the processor EASTL needs: its ARM atomics are written over the compiler builtins (__atomic_*), which wasm32 has, while the x86
    # ones are not meant for wasm. The name is a label only: nothing in the engine reads EA_PLATFORM_*
    add_compile_definitions(EA_PLATFORM_SAMSUNG_TV)
    # EASTL's debug break for ARM is inline assembly ("BKPT 10") that wasm cannot assemble (assert.cpp: "invalid instruction"); a trap is
    # what the web has (the same as ENGINE_DEBUG_BREAK in BECore/Assert/PlatformDebug.h)
    add_compile_definitions(EASTL_DEBUG_BREAK_OVERRIDE=__builtin_trap)
    message(STATUS "Target: web (Emscripten), the four platform options are forced")
endif()

if(ENGINE_WITH_VULKAN)
    message(STATUS "Vulkan: ENABLED (ENGINE_WITH_VULKAN)")
else()
    message(STATUS "Vulkan: DISABLED (ENGINE_WITH_VULKAN)")
endif()

if(ENGINE_WITH_REMOTE_CONTROL)
    add_compile_definitions(BE_WITH_REMOTE_CONTROL=1)
    message(STATUS "RemoteControl: ENABLED (ENGINE_WITH_REMOTE_CONTROL)")
else()
    add_compile_definitions(BE_WITH_REMOTE_CONTROL=0)
    message(STATUS "RemoteControl: DISABLED (ENGINE_WITH_REMOTE_CONTROL)")
endif()

if(ENGINE_STATIC_SDL)
    message(STATUS "SDL: static (ENGINE_STATIC_SDL)")
else()
    message(STATUS "SDL: shared (ENGINE_STATIC_SDL)")
endif()

if(ENGINE_SINGLE_THREAD)
    add_compile_definitions(BE_SINGLE_THREAD=1)
    message(STATUS "Threads: single thread (ENGINE_SINGLE_THREAD)")
else()
    add_compile_definitions(BE_SINGLE_THREAD=0)
    message(STATUS "Threads: worker threads (ENGINE_SINGLE_THREAD)")
endif()
