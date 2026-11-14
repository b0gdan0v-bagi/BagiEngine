# =============================================================================
# Compiler cache: ccache in front of clang / gcc when it is installed
# =============================================================================
# Included once by cmake/BagiEngine.cmake before the first target, so CMAKE_<LANG>_COMPILER_LAUNCHER reaches every target (the
# vendored libraries too).
#
#   ENGINE_CCACHE           ON   Compile through ccache when `ccache` is on the PATH (no ccache: a plain build, a status line).
#                                Off for MSVC and the web build (ccache does not wrap cl.exe and emcc the same way).
#   ENGINE_CCACHE_BASE_DIR  ""   ccache's base_dir: absolute paths under it are hashed relative to the build folder, so two
#                                checkouts (the main one and the workers) share hits. The price: the compiler then sees relative
#                                paths, so __FILE__ (asserts, profiler zones), diagnostics and debug info are relative to the
#                                build folder. Empty: the environment variable of the same name, then the base_dir of the ccache
#                                configuration; none of them: no sharing between checkouts, absolute paths everywhere.
#
# Where base_dir comes from (the status line of the configure says which one is in force). The folder is the one that holds every
# checkout: the main one, the workers folder and the sibling games (tools/todo/worker.py exports it for its sessions and configures).
#   1. base_dir of the ccache configuration or CCACHE_BASEDIR (`ccache --show-config`): it wins, so an owner who set it once in
#      ~/.ccache/ccache.conf (`base_dir = <that folder>`) covers every preset and every game of the machine.
#   2. -DENGINE_CCACHE_BASE_DIR=<folder>, else the environment variable ENGINE_CCACHE_BASE_DIR (read at every configure, so a preset
#      configured later in the same shell or session gets it with no -D).
# With either, hash_dir is switched off for our compiles (with -g the working folder is hashed too, and it differs per checkout).
#
# The settings that the precompiled header needs are given to ccache with every compile (ccache >= 4.8 takes KEY=VALUE before the
# compiler, the launcher script below sets them in its environment): pch_defines and time_macros (a PCH is hashed by its content,
# not its defines and __TIME__), include_file_mtime / include_file_ctime (a header written in the same second as the compile is
# still cached). Clang writes a timestamp into the PCH unless -fno-pch-timestamp; with it the PCH of a rebuild is byte-equal and
# every object that includes it is a hit. `ccache -s` shows the hit rate, `ccache -z` zeroes it.
#
# Sharing between checkouts with the PCH (Clang on macOS and Linux): the PCH of two checkouts is never byte-equal, because Clang
# writes into it the absolute path of every header it read (the working folder joined with the relative path; no option of Clang 17
# or Apple clang 17 makes them relative: -fmodule-file-home-is-cwd is for modules only, the debug and file prefix maps do not touch
# the input files of a PCH), so ccache would hash a different file in each checkout and miss in every object that uses it.
# cmake/BagiCcacheLauncher.sh is the launcher of such a build: it compiles a PCH without ccache and writes <pch>.sum beside it (a
# constant), and ccache (pch_external_checksum) hashes that file instead of the PCH; the headers are in the manifest of every
# object anyway, so a changed header is a miss (the residual risk: a definition or include path private to the PCH's own compile that no header shows changes nothing in the constant; clean the cache entry or change a header after such an edit). Two more things make the PCH's own inputs path-free: the PCH header is given as
# <BECore/pch.h> (CMake writes the argument into cmake_pch.hxx, which ccache hashes: a path there differs per checkout) and the
# vendored SDL, whose own PCH is an absolute path in CMake's cmake_pch.h that we cannot change, is built without its PCH
# (BagiEngine.cmake). A PCH is never taken from the cache: it names the files of the checkout that made it and Clang refuses it
# when they changed ("has been modified since the precompiled header was built"). The probe `tools/ccache/ccache_share.py`
# measures all of this.
include_guard(GLOBAL)

option(ENGINE_CCACHE "Compile through ccache when it is installed" ON)
set(ENGINE_CCACHE_BASE_DIR "" CACHE PATH "ccache base_dir: share cache hits between checkouts under it (paths become relative)")

# The environment is the default of an empty variable (a normal variable: the cache keeps what the user gave with -D).
if(NOT ENGINE_CCACHE_BASE_DIR AND NOT "$ENV{ENGINE_CCACHE_BASE_DIR}" STREQUAL "")
    set(ENGINE_CCACHE_BASE_DIR "$ENV{ENGINE_CCACHE_BASE_DIR}")
    set(BE_CCACHE_BASE_DIR_FROM_ENVIRONMENT ON)
endif()

if(ENGINE_CCACHE AND NOT MSVC AND NOT EMSCRIPTEN)
    find_program(BE_CCACHE_PROGRAM ccache)
    if(BE_CCACHE_PROGRAM)
        execute_process(COMMAND ${BE_CCACHE_PROGRAM} --version OUTPUT_VARIABLE BE_CCACHE_VERSION_TEXT ERROR_QUIET)
        string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _ "${BE_CCACHE_VERSION_TEXT}")
        set(BE_CCACHE_VERSION "${CMAKE_MATCH_1}")

        # The base_dir in force: the ccache configuration's (or CCACHE_BASEDIR's) wins over ours, and then the launcher passes that
        # same value, never a different one.
        execute_process(COMMAND ${BE_CCACHE_PROGRAM} --show-config OUTPUT_VARIABLE BE_CCACHE_CONFIG_TEXT ERROR_QUIET)
        string(REGEX MATCH "base_dir = ([^\n]*)" _ "${BE_CCACHE_CONFIG_TEXT}")
        set(BE_CCACHE_CONFIG_BASE_DIR "${CMAKE_MATCH_1}")
        if(BE_CCACHE_CONFIG_BASE_DIR)
            set(BE_CCACHE_BASE_DIR "${BE_CCACHE_CONFIG_BASE_DIR}")
            set(BE_CCACHE_BASE_DIR_SOURCE "the ccache configuration")
        elseif(ENGINE_CCACHE_BASE_DIR)
            set(BE_CCACHE_BASE_DIR "${ENGINE_CCACHE_BASE_DIR}")
            if(BE_CCACHE_BASE_DIR_FROM_ENVIRONMENT)
                set(BE_CCACHE_BASE_DIR_SOURCE "the environment variable ENGINE_CCACHE_BASE_DIR")
            else()
                set(BE_CCACHE_BASE_DIR_SOURCE "ENGINE_CCACHE_BASE_DIR")
            endif()
        else()
            set(BE_CCACHE_BASE_DIR "")
        endif()

        find_program(BE_SH_PROGRAM sh)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_HOST_UNIX AND BE_SH_PROGRAM)
            # An empty argument would vanish from the list: "-" says there is no base_dir.
            if(BE_CCACHE_BASE_DIR)
                set(BE_CCACHE_BASE_DIR_ARGUMENT "${BE_CCACHE_BASE_DIR}")
            else()
                set(BE_CCACHE_BASE_DIR_ARGUMENT "-")
            endif()
            set(BE_CCACHE_LAUNCHER ${BE_SH_PROGRAM} ${CMAKE_CURRENT_LIST_DIR}/BagiCcacheLauncher.sh ${BE_CCACHE_PROGRAM} ${BE_CCACHE_BASE_DIR_ARGUMENT})
            set(BE_CCACHE_PCH_LAUNCHER ON)
        else()
            set(BE_CCACHE_LAUNCHER ${BE_CCACHE_PROGRAM})
            if(BE_CCACHE_VERSION VERSION_GREATER_EQUAL 4.8)
                list(APPEND BE_CCACHE_LAUNCHER "sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime")
                if(BE_CCACHE_BASE_DIR)
                    # hash_dir off: with -g the working folder is hashed too, and it differs between checkouts
                    list(APPEND BE_CCACHE_LAUNCHER "base_dir=${BE_CCACHE_BASE_DIR}" "hash_dir=false")
                endif()
            else()
                message(STATUS "ccache ${BE_CCACHE_VERSION} is older than 4.8: the PCH settings must be in ccache.conf (cmake/BagiCompilerCache.cmake)")
            endif()
        endif()

        set(CMAKE_C_COMPILER_LAUNCHER ${BE_CCACHE_LAUNCHER})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${BE_CCACHE_LAUNCHER})
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-Xclang -fno-pch-timestamp>")
        endif()
        if(BE_CCACHE_PCH_LAUNCHER)
            message(STATUS "ccache ${BE_CCACHE_VERSION}: ${BE_CCACHE_PROGRAM} (PCH-aware launcher)")
        else()
            message(STATUS "ccache ${BE_CCACHE_VERSION}: ${BE_CCACHE_PROGRAM}")
        endif()
        if(BE_CCACHE_BASE_DIR)
            message(STATUS "ccache base_dir: ${BE_CCACHE_BASE_DIR} (${BE_CCACHE_BASE_DIR_SOURCE})")
            if(ENGINE_CCACHE_BASE_DIR AND BE_CCACHE_CONFIG_BASE_DIR AND NOT ENGINE_CCACHE_BASE_DIR STREQUAL BE_CCACHE_CONFIG_BASE_DIR)
                message(STATUS "ccache base_dir: ENGINE_CCACHE_BASE_DIR=${ENGINE_CCACHE_BASE_DIR} is not used, the ccache configuration has its own")
            endif()
            file(REAL_PATH "${BE_CCACHE_BASE_DIR}" BE_CCACHE_BASE_DIR_REAL)
            file(REAL_PATH "${CMAKE_BINARY_DIR}" BE_CCACHE_BINARY_DIR_REAL)
            cmake_path(IS_PREFIX BE_CCACHE_BASE_DIR_REAL "${BE_CCACHE_BINARY_DIR_REAL}" NORMALIZE BE_CCACHE_BINARY_DIR_UNDER_BASE)
            if(NOT BE_CCACHE_BINARY_DIR_UNDER_BASE)
                message(STATUS "ccache base_dir: this build folder is not under it, so nothing is shared from here")
            endif()
        else()
            message(STATUS "ccache base_dir: none, so this build shares no hits with other checkouts: set ENGINE_CCACHE_BASE_DIR (environment or -D) or base_dir and hash_dir = false in ccache.conf to the folder that holds them")
        endif()
    else()
        message(STATUS "ccache not found: a build without a compiler cache")
    endif()
endif()
