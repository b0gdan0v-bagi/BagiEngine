# =============================================================================
# Compile-time measurement: clang's -ftime-trace
# =============================================================================
# Included once by cmake/BagiEngine.cmake before the first target, so the option reaches every target (the vendored libraries
# too), like ENGINE_CCACHE.
#
#   ENGINE_TIME_TRACE  OFF  Every C/C++ compile writes a Chrome trace (<object>.json next to the object file, the PCH one too).
#                           Clang and clang-cl only; another compiler gets a status line and no change. A ccache hit replays the
#                           object without the .json, so the preset macos-time-trace turns ENGINE_CCACHE off. Read the traces
#                           with CI/scripts/time_trace_report.py.
include_guard(GLOBAL)

option(ENGINE_TIME_TRACE "Write a -ftime-trace Chrome trace next to every object file (Clang only)" OFF)

if(ENGINE_TIME_TRACE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:-ftime-trace>")
        message(STATUS "Compile-time trace: -ftime-trace on every C/C++ compile")
    else()
        message(STATUS "ENGINE_TIME_TRACE needs Clang (compiler is ${CMAKE_CXX_COMPILER_ID}): no trace")
    endif()
endif()
