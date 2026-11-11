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
#                                build folder. Empty: no sharing between checkouts, absolute paths everywhere.
#
# The settings that the precompiled header needs are passed on ccache's command line (ccache >= 4.8 takes KEY=VALUE before the
# compiler), so nobody has to edit their ccache.conf: pch_defines and time_macros (a PCH is hashed by its content, not its
# defines and __TIME__), include_file_mtime / include_file_ctime (a header written in the same second as the compile is still
# cached). Clang writes a timestamp into the PCH unless -fno-pch-timestamp; with it the PCH of a rebuild is byte-equal and
# every object that includes it is a hit. `ccache -s` shows the hit rate, `ccache -z` zeroes it.
include_guard(GLOBAL)

option(ENGINE_CCACHE "Compile through ccache when it is installed" ON)
set(ENGINE_CCACHE_BASE_DIR "" CACHE PATH "ccache base_dir: share cache hits between checkouts under it (paths become relative)")

if(ENGINE_CCACHE AND NOT MSVC AND NOT EMSCRIPTEN)
    find_program(BE_CCACHE_PROGRAM ccache)
    if(BE_CCACHE_PROGRAM)
        execute_process(COMMAND ${BE_CCACHE_PROGRAM} --version OUTPUT_VARIABLE BE_CCACHE_VERSION_TEXT ERROR_QUIET)
        string(REGEX MATCH "version ([0-9]+\\.[0-9]+)" _ "${BE_CCACHE_VERSION_TEXT}")
        set(BE_CCACHE_VERSION "${CMAKE_MATCH_1}")

        set(BE_CCACHE_LAUNCHER ${BE_CCACHE_PROGRAM})
        if(BE_CCACHE_VERSION VERSION_GREATER_EQUAL 4.8)
            list(APPEND BE_CCACHE_LAUNCHER "sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime")
            if(ENGINE_CCACHE_BASE_DIR)
                # hash_dir off: with -g the working folder is hashed too, and it differs between checkouts
                list(APPEND BE_CCACHE_LAUNCHER "base_dir=${ENGINE_CCACHE_BASE_DIR}" "hash_dir=false")
            endif()
        else()
            message(STATUS "ccache ${BE_CCACHE_VERSION} is older than 4.8: the PCH settings must be in ccache.conf (cmake/BagiCompilerCache.cmake)")
        endif()

        set(CMAKE_C_COMPILER_LAUNCHER ${BE_CCACHE_LAUNCHER})
        set(CMAKE_CXX_COMPILER_LAUNCHER ${BE_CCACHE_LAUNCHER})
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:SHELL:-Xclang -fno-pch-timestamp>")
        endif()
        message(STATUS "ccache ${BE_CCACHE_VERSION}: ${BE_CCACHE_PROGRAM}")
    else()
        message(STATUS "ccache not found: a build without a compiler cache")
    endif()
endif()
