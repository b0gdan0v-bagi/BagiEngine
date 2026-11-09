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

# An "@" in a source path is written "@@" (emcc's file packager: a single "@" splits the source from the virtual path)
file(MAKE_DIRECTORY "${CHECK_DIR}/at@dir/config")
be_web_preload_args(OUT preload ENGINE_ROOT "${CHECK_DIR}/at@dir")
check_equal("preload, an @ in the path" "${preload}" "--preload-file ${CHECK_DIR}/at@@dir/config@/bagi/engine/config")

# --- be_web_content_inputs: the files of a folder plus a stamp with the sorted list of them, rewritten only when the list changes
set(content "${CHECK_DIR}/content")
set(stamp "${CHECK_DIR}/stamps/content.txt")
file(WRITE "${content}/b.txt" "b")
file(WRITE "${content}/sub/a@x@.txt" "a")
be_web_content_inputs(OUT inputs FOLDER "${content}" STAMP "${stamp}")
check_equal("content inputs" "${inputs}" "${content}/b.txt;${content}/sub/a@x@.txt;${stamp}")
file(READ "${stamp}" stamp_text)
check_equal("stamp text" "${stamp_text}" "${content}/b.txt\n${content}/sub/a%40x%40.txt\n")

# The same list: the stamp is not touched (the time is read to the second, so wait longer than that)
file(TIMESTAMP "${stamp}" stamp_time "%Y-%m-%d %H:%M:%S")
execute_process(COMMAND ${CMAKE_COMMAND} -E sleep 1.5)
be_web_content_inputs(OUT inputs FOLDER "${content}" STAMP "${stamp}")
file(TIMESTAMP "${stamp}" stamp_time_again "%Y-%m-%d %H:%M:%S")
check_equal("stamp untouched when the list is the same" "${stamp_time_again}" "${stamp_time}")

# An added file and a deleted one both change the stamp (a deletion is the case that no input of the link shows)
file(WRITE "${content}/c.txt" "c")
be_web_content_inputs(OUT inputs FOLDER "${content}" STAMP "${stamp}")
file(READ "${stamp}" stamp_added)
check_equal("stamp, file added" "${stamp_added}" "${content}/b.txt\n${content}/c.txt\n${content}/sub/a%40x%40.txt\n")
file(REMOVE "${content}/b.txt" "${content}/c.txt")
be_web_content_inputs(OUT inputs FOLDER "${content}" STAMP "${stamp}")
file(READ "${stamp}" stamp_removed)
check_equal("stamp, files deleted" "${stamp_removed}" "${content}/sub/a%40x%40.txt\n")
check_equal("content inputs after the deletion" "${inputs}" "${content}/sub/a@x@.txt;${stamp}")

# --- be_web_link_options: the same list for every configuration, the assertions behind a generator expression
be_web_link_options(OUT options)
foreach(required -sALLOW_MEMORY_GROWTH=1 -sMIN_WEBGL_VERSION=1 -sMAX_WEBGL_VERSION=2 -sEXIT_RUNTIME=1)
    list(FIND options "${required}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "link options lack ${required}: ${options}")
    endif()
endforeach()
list(FIND options "-sASSERTIONS=1" index)
if(NOT index EQUAL -1)
    message(FATAL_ERROR "the assertions are not behind a generator expression: ${options}")
endif()
list(FIND options "$<$<CONFIG:Debug>:-sASSERTIONS=1>" index)
if(index EQUAL -1)
    message(FATAL_ERROR "link options lack the assertions for Debug: ${options}")
endif()

# --- the HTML shell: the file exists and the link options name it, as one SHELL: entry
check_equal("shell file" "${BE_WEB_SHELL_FILE}" "${BAGI_ENGINE_DIR}/web/shell.html")
if(NOT EXISTS "${BE_WEB_SHELL_FILE}")
    message(FATAL_ERROR "the shell file does not exist: ${BE_WEB_SHELL_FILE}")
endif()
list(FIND options "SHELL:--shell-file \"${BE_WEB_SHELL_FILE}\"" index)
if(index EQUAL -1)
    message(FATAL_ERROR "link options lack --shell-file: ${options}")
endif()

message("OK")
