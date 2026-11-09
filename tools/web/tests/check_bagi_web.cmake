# Checks of the pure functions of cmake/BagiWeb.cmake, run by test_cmake_web.py:
#   cmake -DBAGI_ENGINE_DIR=<engine checkout> -DCHECK_DIR=<scratch folder> -P tools/web/tests/check_bagi_web.cmake
# Prints "OK" at the end; the first failed check stops with a FATAL_ERROR that names it.
cmake_minimum_required(VERSION 3.31)

if(NOT BAGI_ENGINE_DIR OR NOT CHECK_DIR)
    message(FATAL_ERROR "usage: cmake -DBAGI_ENGINE_DIR=<dir> -DCHECK_DIR=<scratch dir> -P check_bagi_web.cmake")
endif()

include(${BAGI_ENGINE_DIR}/cmake/BagiWeb.cmake)

function(check_equal NAME ACTUAL EXPECTED)
    if(NOT "${ACTUAL}" STREQUAL "${EXPECTED}")
        message(FATAL_ERROR "${NAME}:\n  expected: ${EXPECTED}\n  actual:   ${ACTUAL}")
    endif()
endfunction()

# --- the output suffix and the virtual roots
check_equal("suffix" "${BE_WEB_OUTPUT_SUFFIX}" ".html")
check_equal("engine root" "${BE_WEB_ENGINE_ROOT}" "/bagi/engine")
check_equal("game root" "${BE_WEB_GAME_ROOT}" "/bagi/game")
check_equal("web root" "${BE_WEB_WEB_ROOT}" "/bagi/web")

# --- be_web_preload_args: config/ and assets/ of each layer that has one, lowest priority first
file(MAKE_DIRECTORY "${CHECK_DIR}/engine/config" "${CHECK_DIR}/engine/assets"
                    "${CHECK_DIR}/game/config" "${CHECK_DIR}/game/assets"
                    "${CHECK_DIR}/web/config")
be_web_preload_args(OUT preload
    ENGINE_ROOT "${CHECK_DIR}/engine" GAME_ROOT "${CHECK_DIR}/game" WEB_ROOT "${CHECK_DIR}/web")
set(expected
    "--preload-file ${CHECK_DIR}/engine/config@/bagi/engine/config"
    "--preload-file ${CHECK_DIR}/engine/assets@/bagi/engine/assets"
    "--preload-file ${CHECK_DIR}/game/config@/bagi/game/config"
    "--preload-file ${CHECK_DIR}/game/assets@/bagi/game/assets"
    "--preload-file ${CHECK_DIR}/web/config@/bagi/web/config")
check_equal("preload, all layers" "${preload}" "${expected}")

# A layer without assets/ gets no entry for it; a layer that is not given gets none at all
file(REMOVE_RECURSE "${CHECK_DIR}/game/assets")
be_web_preload_args(OUT preload ENGINE_ROOT "${CHECK_DIR}/engine" GAME_ROOT "${CHECK_DIR}/game")
set(expected
    "--preload-file ${CHECK_DIR}/engine/config@/bagi/engine/config"
    "--preload-file ${CHECK_DIR}/engine/assets@/bagi/engine/assets"
    "--preload-file ${CHECK_DIR}/game/config@/bagi/game/config")
check_equal("preload, no game assets, no web layer" "${preload}" "${expected}")

# --- be_web_link_options per build type
be_web_link_options(OUT release CONFIG Release)
be_web_link_options(OUT debug CONFIG Debug)
foreach(required -sALLOW_MEMORY_GROWTH=1 -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=1)
    list(FIND release "${required}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "Release link options lack ${required}: ${release}")
    endif()
    list(FIND debug "${required}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "Debug link options lack ${required}: ${debug}")
    endif()
endforeach()
list(FIND release "-sASSERTIONS=1" index)
if(NOT index EQUAL -1)
    message(FATAL_ERROR "Release link options carry the assertions: ${release}")
endif()
list(FIND debug "-sASSERTIONS=1" index)
if(index EQUAL -1)
    message(FATAL_ERROR "Debug link options lack -sASSERTIONS=1: ${debug}")
endif()
list(LENGTH release releaseCount)
list(LENGTH debug debugCount)
math(EXPR expectedDebugCount "${releaseCount} + 1")
check_equal("Debug differs from Release by the assertions only" "${debugCount}" "${expectedDebugCount}")

message("OK")
