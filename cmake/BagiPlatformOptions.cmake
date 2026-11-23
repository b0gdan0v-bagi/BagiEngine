# =============================================================================
# Platform options: what the build contains besides the core (Vulkan, the socket server, SDL linkage, threads)
# =============================================================================
# Included once by cmake/BagiEngine.cmake after the C++ standard block and before the SDL block.
#
#   ENGINE_WITH_VULKAN          ON   VulkanModule: the Vulkan renderer backend and its loader library.
#   ENGINE_WITH_REMOTE_CONTROL  ON   The sockets of the engine: RemoteControlModule (the WebSocket server of the control API and the WebSocket
#                                    client) with its start in Application, RoomServer, and the desktop transport of RoomClient. RoomClient
#                                    itself is built without it (the browser transport is its web half). Forced OFF on the web.
#   ENGINE_STATIC_SDL           OFF  SDL3 and SDL3_image as static libraries. The target name SDL3-shared stays (an INTERFACE
#                                    shim over SDL3::SDL3-static, made in cmake/BagiEngine.cmake), because the vendored imgui
#                                    CMake file and our modules link that name.
#   ENGINE_SINGLE_THREAD        OFF  The task system creates no thread (the definition BE_SINGLE_THREAD=1; TaskSystem/CLAUDE.md).
#   ENGINE_PROFILER             OFF  The BE_PROFILE_* macros record zones, frames, counters (BE_PROFILER=1; BECore/Profiler/CLAUDE.md).
#                                    The recorder and the Chrome-trace writer compile in every build; only the macros compile out.
#   ENGINE_PROFILER_ALLOC       OFF  The global operator new / delete are replaced to count allocations (BE_PROFILER_ALLOC=1).
#                                    Needs ENGINE_PROFILER.
#   ENGINE_PROFILER_TRACY       OFF  The vendored Tracy client and its backend (BE_PROFILER_TRACY=1). Needs ENGINE_PROFILER.
#   ENGINE_PROFILER_STATS       OFF  The stats build (BE_PROFILER_STATS=1): ON forces ENGINE_PROFILER ON, also on the web, where this is the one way
#                                    to switch the profiler on (the frame-ring overlay of a shipped page). It adds no allocation counting and no Tracy.
#
# The browser (Emscripten) build has no Vulkan, no sockets server and no shared libraries, so on EMSCRIPTEN those three are forced
# to the values above; the profiler is for the PC, so ENGINE_PROFILER follows ENGINE_PROFILER_STATS there (off unless the stats build is asked for) and
# ENGINE_PROFILER_ALLOC and ENGINE_PROFILER_TRACY are forced off. Threads are not forced: the default there is OFF like everywhere, which is a threaded page (-pthread and a
# pool of Web Workers, the page then needs cross-origin isolation); ENGINE_SINGLE_THREAD=ON is the single-thread page (the presets
# web-*-single-thread). ENGINE_TARGET_WEB mirrors EMSCRIPTEN for the other cmake files. A desktop build can switch the options one
# by one to check the web configuration without an Emscripten SDK (cmake/CLAUDE.md, "Web-like build on the desktop").
include_guard(GLOBAL)

if(EMSCRIPTEN)
    set(ENGINE_TARGET_WEB TRUE)
else()
    set(ENGINE_TARGET_WEB FALSE)
endif()

option(ENGINE_WITH_VULKAN "Build the Vulkan renderer backend (VulkanModule)" ON)
option(ENGINE_WITH_REMOTE_CONTROL "Build the sockets of the engine: RemoteControlModule (the control API server, the WebSocket client) and RoomServer" ON)
option(ENGINE_STATIC_SDL "Build SDL3 and SDL3_image as static libraries" OFF)
option(ENGINE_SINGLE_THREAD "Run the task system without worker threads" OFF)
option(ENGINE_PROFILER "Record the BE_PROFILE_* zones, frames and counters" OFF)
option(ENGINE_PROFILER_ALLOC "Replace the global operator new / delete to count allocations (needs ENGINE_PROFILER)" OFF)
option(ENGINE_PROFILER_TRACY "Build the vendored Tracy client and its profiler backend (needs ENGINE_PROFILER)" OFF)
option(ENGINE_PROFILER_STATS "The stats build: the profiler on, with the frame-ring overlay and the trace export, also on the web" OFF)

# The stats build is the one way to get the profiler on the web, where ENGINE_PROFILER alone is forced off
if(ENGINE_PROFILER_STATS)
    set(ENGINE_PROFILER ON CACHE BOOL "Record the BE_PROFILE_* zones, frames and counters" FORCE)
    message(STATUS "Profiler stats build: ENGINE_PROFILER_STATS=ON (zones into the frame ring, the overlay, the trace export)")
endif()

if(ENGINE_TARGET_WEB)
    set(ENGINE_WITH_VULKAN OFF CACHE BOOL "Build the Vulkan renderer backend (VulkanModule)" FORCE)
    set(ENGINE_WITH_REMOTE_CONTROL OFF CACHE BOOL "Build the sockets of the engine: RemoteControlModule (the control API server, the WebSocket client) and RoomServer" FORCE)
    set(ENGINE_STATIC_SDL ON CACHE BOOL "Build SDL3 and SDL3_image as static libraries" FORCE)
    set(ENGINE_PROFILER ${ENGINE_PROFILER_STATS} CACHE BOOL "Record the BE_PROFILE_* zones, frames and counters" FORCE)
    set(ENGINE_PROFILER_ALLOC OFF CACHE BOOL "Replace the global operator new / delete to count allocations (needs ENGINE_PROFILER)" FORCE)
    set(ENGINE_PROFILER_TRACY OFF CACHE BOOL "Build the vendored Tracy client and its profiler backend (needs ENGINE_PROFILER)" FORCE)
    add_compile_definitions(PLATFORM_WEB)
    # EABase (vendored) knows no Emscripten: without help it stops with "Unknown platform / processor". Its only branch that takes the
    # platform from a definition and probes no compiler macro is EA_PLATFORM_SAMSUNG_TV (Linux, POSIX, little endian, ARM32). ARM32 is
    # the processor EASTL needs: its ARM atomics are written over the compiler builtins (__atomic_*), which wasm32 has, while the x86
    # ones are not meant for wasm. The name is a label only: nothing in the engine reads EA_PLATFORM_*
    # A trap for threads: not all of EASTL's ARM code is builtins. eastl::cpu_pause() is asm("yield") and the memory barrier of
    # eastl::atomic (EASTL_ARCH_ATOMIC_CPU_MB) is asm("dmb ish"), which wasm cannot assemble. The engine uses std::atomic and
    # std::mutex everywhere (no eastl::atomic, no cpu_pause: grep src tools) and so the threaded build compiles; keep it so, or
    # that code fails to compile on the web with "invalid instruction"
    add_compile_definitions(EA_PLATFORM_SAMSUNG_TV)
    # EASTL's debug break for ARM is inline assembly ("BKPT 10") that wasm cannot assemble (assert.cpp: "invalid instruction"); a trap is
    # what the web has (the same as ENGINE_DEBUG_BREAK in BECore/Assert/PlatformDebug.h)
    add_compile_definitions(EASTL_DEBUG_BREAK_OVERRIDE=__builtin_trap)
    message(STATUS "Target: web (Emscripten), Vulkan, remote control and shared SDL are forced off, the profiler unless ENGINE_PROFILER_STATS is ON")
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

# The profiler: the definitions are always 0 or 1, so code tests them with #if, never #ifdef
if((ENGINE_PROFILER_ALLOC OR ENGINE_PROFILER_TRACY) AND NOT ENGINE_PROFILER)
    message(FATAL_ERROR "ENGINE_PROFILER_ALLOC and ENGINE_PROFILER_TRACY need ENGINE_PROFILER=ON")
endif()
if(ENGINE_PROFILER)
    add_compile_definitions(BE_PROFILER=1)
else()
    add_compile_definitions(BE_PROFILER=0)
endif()
if(ENGINE_PROFILER_ALLOC)
    add_compile_definitions(BE_PROFILER_ALLOC=1)
    # EASTL passes the name of every allocation (a container's allocator name, or the one set by set_name) to its operator new only
    # when EASTL_NAME_ENABLED, which follows EASTL_DEBUG, which follows _DEBUG: no macOS or Linux build defines that. Level 1 passes
    # the name alone; level 2 would add file and line, but eastl::allocator::allocate is an inline function of allocator.h that
    # passes its own __FILE__ / __LINE__, so it never names the caller. Names add a member to eastl::allocator (a container gets
    # larger), so the definitions are global, set before EABase is added: EASTL's sources, the engine, the tests and every game must
    # agree, and only with this option so the shipped layout does not change.
    add_compile_definitions(EASTL_NAME_ENABLED=1 EASTL_DEBUGPARAMS_LEVEL=1)
else()
    add_compile_definitions(BE_PROFILER_ALLOC=0)
endif()
if(ENGINE_PROFILER_TRACY)
    add_compile_definitions(BE_PROFILER_TRACY=1)
else()
    add_compile_definitions(BE_PROFILER_TRACY=0)
endif()
if(ENGINE_PROFILER_STATS)
    add_compile_definitions(BE_PROFILER_STATS=1)
else()
    add_compile_definitions(BE_PROFILER_STATS=0)
endif()
message(STATUS "Profiler: ${ENGINE_PROFILER} (allocations ${ENGINE_PROFILER_ALLOC}, Tracy ${ENGINE_PROFILER_TRACY})")
