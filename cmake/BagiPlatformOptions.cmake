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
# The browser (Emscripten) build has no Vulkan, no sockets server and no shared libraries, so on EMSCRIPTEN those three are forced
# to the values above. Threads are not forced: the default there is OFF like everywhere, which is a threaded page (-pthread and a
# pool of Web Workers, the page then needs cross-origin isolation); ENGINE_SINGLE_THREAD=ON is the single-thread page (the presets
# web-*-single-thread). ENGINE_TARGET_WEB mirrors EMSCRIPTEN for the other cmake files. A desktop build can switch the options one
# by one to check the web configuration without an Emscripten SDK (root CLAUDE.md, "Build Commands").
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
    add_compile_definitions(PLATFORM_WEB)
    # EABase (vendored) knows no Emscripten: without help it stops with "Unknown platform / processor". Its only branch that takes the
    # platform from a definition and probes no compiler macro is EA_PLATFORM_SAMSUNG_TV (Linux, POSIX, little endian, ARM32). ARM32 is
    # the processor EASTL needs: its ARM atomics are written over the compiler builtins (__atomic_*), which wasm32 has, while the x86
    # ones are not meant for wasm. The name is a label only: nothing in the engine reads EA_PLATFORM_*
    # A trap for threads: not all of EASTL's ARM code is builtins. eastl::cpu_pause() is asm("yield") and the memory barrier of
    # eastl::atomic (EASTL_ARCH_ATOMIC_CPU_MB) is asm("dmb ish"), which wasm cannot assemble. The engine uses std::atomic and
    # std::mutex everywhere (no eastl::atomic, no cpu_pause: grep src games) and so the threaded build compiles; keep it so, or
    # that code fails to compile on the web with "invalid instruction"
    add_compile_definitions(EA_PLATFORM_SAMSUNG_TV)
    # EASTL's debug break for ARM is inline assembly ("BKPT 10") that wasm cannot assemble (assert.cpp: "invalid instruction"); a trap is
    # what the web has (the same as ENGINE_DEBUG_BREAK in BECore/Assert/PlatformDebug.h)
    add_compile_definitions(EASTL_DEBUG_BREAK_OVERRIDE=__builtin_trap)
    message(STATUS "Target: web (Emscripten), Vulkan, remote control and shared SDL are forced off")
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
    if(ENGINE_TARGET_WEB)
        # Atomics and shared memory need the same -pthread in every object of the link, the vendored SDL, SDL_image, EASTL and fmt
        # included (wasm-ld refuses a mix: "--shared-memory is disallowed by <file> because it was not compiled with 'atomics'"). The
        # flags are plain variables set before the first add_subdirectory, so every target of every directory gets them and so do the
        # configure-time tests of SDL (try_compile reads CMAKE_<LANG>_FLAGS). The link gets it from be_web_link_options() and from
        # CMAKE_EXE_LINKER_FLAGS for the same reason. The size of the Web Worker pool is BE_WEB_THREAD_POOL_SIZE (cmake/BagiWeb.cmake)
        string(APPEND CMAKE_C_FLAGS " -pthread")
        string(APPEND CMAKE_CXX_FLAGS " -pthread")
        string(APPEND CMAKE_EXE_LINKER_FLAGS " -pthread")
        # Read by TaskManager.cpp: a static_assert that the pool holds the engine's worker threads (kMaxWorkerThreads)
        add_compile_definitions(BE_WEB_THREAD_POOL_SIZE=${BE_WEB_THREAD_POOL_SIZE})
        message(STATUS "Web threads: -pthread, a pool of ${BE_WEB_THREAD_POOL_SIZE} Web Workers (BE_WEB_THREAD_POOL_SIZE)")
    endif()
endif()
