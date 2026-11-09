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

# --- be_web_parse_preload_spec undoes be_web_preload_args, an "@" in the source included
foreach(entry IN LISTS preload)
    string(REPLACE "--preload-file " "" spec "${entry}")
    be_web_parse_preload_spec(SPEC "${spec}" FOLDER parsed_folder VIRTUAL parsed_virtual)
    check_equal("parsed folder, an @ in the path" "${parsed_folder}" "${CHECK_DIR}/at@dir/config")
    check_equal("parsed virtual path" "${parsed_virtual}" "/bagi/engine/config")
endforeach()
be_web_parse_preload_spec(SPEC "${CHECK_DIR}/plain/assets@/bagi/game/assets" FOLDER parsed_folder VIRTUAL parsed_virtual)
check_equal("parsed folder" "${parsed_folder}" "${CHECK_DIR}/plain/assets")
check_equal("parsed virtual path, game" "${parsed_virtual}" "/bagi/game/assets")
be_web_parse_preload_spec(SPEC "${CHECK_DIR}/a@@b@@@c/config@/bagi/web/config" FOLDER parsed_folder VIRTUAL parsed_virtual)
check_equal("parsed folder, two @" "${parsed_folder}" "${CHECK_DIR}/a@b@@c/config")

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

# --- threads: the threaded list has -pthread and the pool, the single-thread list has neither
set(thread_options -pthread -sPTHREAD_POOL_SIZE=${BE_WEB_THREAD_POOL_SIZE} -sPTHREAD_POOL_SIZE_STRICT=2 -sMAXIMUM_MEMORY=${BE_WEB_MAXIMUM_MEMORY} -Wno-pthreads-mem-growth)
check_equal("pool size" "${BE_WEB_THREAD_POOL_SIZE}" "5")
foreach(required IN LISTS thread_options)
    list(FIND options "${required}" index)
    if(index EQUAL -1)
        message(FATAL_ERROR "the threaded link options lack ${required}: ${options}")
    endif()
endforeach()
be_web_link_options(OUT single_options SINGLE_THREAD)
foreach(forbidden IN LISTS thread_options)
    list(FIND single_options "${forbidden}" index)
    if(NOT index EQUAL -1)
        message(FATAL_ERROR "the single-thread link options have ${forbidden}: ${single_options}")
    endif()
endforeach()
# What the single-thread list shares with the threaded one stays: the same settings, only the thread ones differ
set(options_without_threads ${options})
list(REMOVE_ITEM options_without_threads ${thread_options})
check_equal("single-thread options are the threaded ones without the thread settings" "${single_options}" "${options_without_threads}")
# The pool holds the TaskManager's workers and one more (TaskManager.cpp checks the same at compile time through the definition)
file(READ "${BAGI_ENGINE_DIR}/src/Modules/TaskSystem/src/TaskSystem/TaskManager.h" task_manager_header)
if(NOT task_manager_header MATCHES "kMaxWorkerThreads = ([0-9]+);")
    message(FATAL_ERROR "TaskManager.h has no kMaxWorkerThreads")
endif()
math(EXPR needed "${CMAKE_MATCH_1} + 1")
if(BE_WEB_THREAD_POOL_SIZE LESS needed)
    message(FATAL_ERROR "BE_WEB_THREAD_POOL_SIZE ${BE_WEB_THREAD_POOL_SIZE} is below kMaxWorkerThreads + 1 = ${needed}")
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

# --- be_web_generate_shell: a copy of the shell with the placeholder replaced, 1 for a threaded build and 0 for a single-thread one
file(READ "${BE_WEB_SHELL_FILE}" shell_source)
string(FIND "${shell_source}" "@BAGI_WEB_THREADS@" placeholder_at)
if(placeholder_at EQUAL -1)
    message(FATAL_ERROR "web/shell.html has no @BAGI_WEB_THREADS@ placeholder")
endif()
be_web_generate_shell(OUTPUT "${CHECK_DIR}/shell/threaded.html")
be_web_generate_shell(OUTPUT "${CHECK_DIR}/shell/single.html" SINGLE_THREAD)
file(READ "${CHECK_DIR}/shell/threaded.html" shell_threaded)
file(READ "${CHECK_DIR}/shell/single.html" shell_single)
string(FIND "${shell_threaded}" "var threaded = \"1\" === \"1\";" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the threaded copy of the shell does not say threaded = 1")
endif()
string(FIND "${shell_single}" "var threaded = \"0\" === \"1\";" at)
if(at EQUAL -1)
    message(FATAL_ERROR "the single-thread copy of the shell does not say threaded = 0")
endif()
foreach(copy shell_threaded shell_single)
    string(FIND "${${copy}}" "@BAGI_WEB_THREADS@" at)
    if(NOT at EQUAL -1)
        message(FATAL_ERROR "${copy}: the placeholder is still there")
    endif()
    # Nothing else changed: the CSS at-rules, the SCRIPT placeholder of emcc and the length apart from the placeholder's own
    string(FIND "${${copy}}" "@supports (height: 100dvh)" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "${copy}: the CSS @supports rule was lost")
    endif()
    string(FIND "${${copy}}" "{{{ SCRIPT }}}" at)
    if(at EQUAL -1)
        message(FATAL_ERROR "${copy}: the SCRIPT placeholder of emcc was lost")
    endif()
endforeach()
string(LENGTH "${shell_source}" source_length)
string(LENGTH "${shell_threaded}" threaded_length)
math(EXPR expected_length "${source_length} - 18 + 1")
check_equal("threaded copy differs from the source by the placeholder only" "${threaded_length}" "${expected_length}")
# The link options name the copy when they are given one
be_web_link_options(OUT copy_options SHELL_FILE "${CHECK_DIR}/shell/threaded.html")
list(FIND copy_options "SHELL:--shell-file \"${CHECK_DIR}/shell/threaded.html\"" index)
if(index EQUAL -1)
    message(FATAL_ERROR "link options do not name the SHELL_FILE: ${copy_options}")
endif()

message("OK")
