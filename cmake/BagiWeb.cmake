# =============================================================================
# Web (Emscripten) build: link options, content package, the executable as an HTML page
# =============================================================================
# Included by cmake/BagiEngine.cmake. The functions below do not depend on the rest of the engine setup (the cmake -P test
# tools/web/tests/check_bagi_web.cmake includes this file alone), so the pure ones can be checked on a desktop without an
# Emscripten SDK. Only be_web_configure_target() touches a target.
#
# The settings below were checked against settings.js of emsdk 4.0.23 and linked with it (BagiMatch3 links, packages and the
# module instantiates under node; the run in a browser is the entry web-build, slice 2, last work package).
#
#   be_web_preload_args(OUT <var> ENGINE_ROOT <dir> GAME_ROOT <dir> WEB_ROOT <dir>)
#       The content as a preloaded package: one "--preload-file <dir>@<virtual dir>" entry per config/ and assets/ folder that exists,
#       in the order of the layers (engine, game, web: lowest priority first, as AddContentRoot in src/main.cpp). The virtual roots are
#       BE_WEB_ENGINE_ROOT, BE_WEB_GAME_ROOT and BE_WEB_WEB_ROOT, the values of BAGI_ENGINE_ROOT / BAGI_GAME_ROOT / BAGI_WEB_ROOT.
#       An "@" in a source path is written "@@", which is how emcc's file packager takes a literal "@" (the single "@" splits the
#       source from the virtual path).
#   be_web_parse_preload_spec(SPEC <spec> FOLDER <var> VIRTUAL <var>)
#       The inverse of one entry of be_web_preload_args ("<source>@<virtual>", without "--preload-file "): the source folder as it is on
#       disk ("@@" back to "@") and the virtual path. The virtual paths are ours and have no "@", so the last "@" splits.
#   be_web_content_inputs(OUT <var> FOLDER <dir> STAMP <file>)
#       The link inputs that make the content package follow its folder: every file of <dir> plus <file>, a stamp that holds the sorted
#       list of those files. An edit relinks because a file is newer than the .data package; an added or a deleted file relinks because
#       the stamp is rewritten (only when the list changes) at the configure step that CONFIGURE_DEPENDS triggers.
#   be_web_generate_shell(OUTPUT <file> [SINGLE_THREAD])
#       Writes a copy of web/shell.html to <file> with the placeholder @BAGI_WEB_THREADS@ replaced by 1 (threaded) or 0 (SINGLE_THREAD).
#       The page that is not cross-origin isolated shows a message instead of starting only when the build is threaded, and the shell
#       cannot know that by itself. configure_file(), so the copy is rewritten only when its text changes and a change of web/shell.html
#       configures again.
#   be_web_link_options(OUT <var> [SINGLE_THREAD] [SHELL_FILE <file>])
#       The Emscripten link options: the "-s..." settings, the HTML shell (web/shell.html, or SHELL_FILE: the copy of
#       be_web_generate_shell), and the assertions in Debug only (as a generator expression, so the list is the same for every
#       configuration). Without SINGLE_THREAD it also has -pthread and the Web Worker pool (BE_WEB_THREAD_POOL_SIZE); with
#       SINGLE_THREAD it has neither (no thread, no shared memory).
#   be_web_configure_target(<target> GAME_ROOT <dir>)
#       Makes the executable an HTML page with its content package and the virtual content roots as definitions.
include_guard(GLOBAL)

# The page the link produces is <target>.html next to <target>.js, <target>.wasm and <target>.data
set(BE_WEB_OUTPUT_SUFFIX ".html")

# Where the content layers appear in the virtual file system of the page (the engine's config/ and assets/, the game's on top, the web overlay last)
set(BE_WEB_ENGINE_ROOT "/bagi/engine")
set(BE_WEB_GAME_ROOT "/bagi/game")
set(BE_WEB_WEB_ROOT "/bagi/web")

# The Web Workers created before main() in a threaded page (-sPTHREAD_POOL_SIZE). The engine blocks the main thread at start-up
# (ConfigManager::Initialize waits for the config loads on the TaskManager's workers), and a worker that only the main thread's event
# loop could create would never start: the pool has to be ready before main(). So it must hold every thread that starts then:
# kMaxWorkerThreads of the TaskSystem (src/Modules/TaskSystem/src/TaskSystem/TaskManager.h, 4: the ThreadPool of the TaskManager) plus
# one of headroom (SDL starts no thread at start-up as far as its source shows; a timer thread of SDL_AddTimer would be one). TaskManager.cpp has a static_assert on
# it (this value is passed there as the definition BE_WEB_THREAD_POOL_SIZE, cmake/BagiPlatformOptions.cmake). With
# -sPTHREAD_POOL_SIZE_STRICT=2 a thread over the pool is an error at once, not a deadlock of the main thread
set(BE_WEB_THREAD_POOL_SIZE 5)

# The most the page may grow its heap to. ALLOW_MEMORY_GROWTH with shared memory (threads) needs the maximum to be fixed when the
# memory is created: the browser reserves that much address space (not RAM). 2 GB is the default MAXIMUM_MEMORY of emsdk 4.0.23, named
# here so that the limit is written down (a game that needs more can raise it, up to 4 GB for a 32-bit wasm)
set(BE_WEB_MAXIMUM_MEMORY 2147483648)

# The folder of this engine checkout that holds the web files (web/config/, web/shell.html)
get_filename_component(BE_WEB_DIR "${CMAKE_CURRENT_LIST_DIR}/../web" ABSOLUTE)

# The page around the wasm: loading bar, WebGL test, error messages (web/shell.html)
set(BE_WEB_SHELL_FILE "${BE_WEB_DIR}/shell.html")

function(be_web_preload_args)
    cmake_parse_arguments(ARG "" "OUT;ENGINE_ROOT;GAME_ROOT;WEB_ROOT" "" ${ARGN})
    if(NOT ARG_OUT)
        message(FATAL_ERROR "be_web_preload_args: OUT is required")
    endif()

    set(_args "")
    # Lowest priority first, the order of AddContentRoot in src/main.cpp
    foreach(_layer ENGINE GAME WEB)
        set(_root "${ARG_${_layer}_ROOT}")
        if(NOT _root)
            continue()
        endif()
        foreach(_folder config assets)
            if(IS_DIRECTORY "${_root}/${_folder}")
                string(REPLACE "@" "@@" _source "${_root}/${_folder}")
                list(APPEND _args "--preload-file ${_source}@${BE_WEB_${_layer}_ROOT}/${_folder}")
            endif()
        endforeach()
    endforeach()
    set(${ARG_OUT} "${_args}" PARENT_SCOPE)
endfunction()

function(be_web_parse_preload_spec)
    cmake_parse_arguments(ARG "" "SPEC;FOLDER;VIRTUAL" "" ${ARGN})
    if(NOT ARG_SPEC OR NOT ARG_FOLDER OR NOT ARG_VIRTUAL)
        message(FATAL_ERROR "be_web_parse_preload_spec: SPEC, FOLDER and VIRTUAL are required")
    endif()
    string(REGEX REPLACE "@[^@]*$" "" _folder "${ARG_SPEC}")
    string(REPLACE "@@" "@" _folder "${_folder}")
    string(REGEX REPLACE "^.*@" "" _virtual "${ARG_SPEC}")
    set(${ARG_FOLDER} "${_folder}" PARENT_SCOPE)
    set(${ARG_VIRTUAL} "${_virtual}" PARENT_SCOPE)
endfunction()

function(be_web_content_inputs)
    cmake_parse_arguments(ARG "" "OUT;FOLDER;STAMP" "" ${ARGN})
    if(NOT ARG_OUT OR NOT ARG_FOLDER OR NOT ARG_STAMP)
        message(FATAL_ERROR "be_web_content_inputs: OUT, FOLDER and STAMP are required")
    endif()

    # CONFIGURE_DEPENDS is refused in script mode (cmake -P, the pure check of this function), where nothing is built anyway
    if(CMAKE_SCRIPT_MODE_FILE)
        file(GLOB_RECURSE _files "${ARG_FOLDER}/*")
    else()
        file(GLOB_RECURSE _files CONFIGURE_DEPENDS "${ARG_FOLDER}/*")
    endif()
    list(SORT _files)
    # Ninja reruns the link only when an input is newer than the output, so a deleted file leaves no trace in the inputs: the stamp
    # carries the list. file(CONFIGURE) leaves the file alone when its content is the same. "@" is written as "%40" because the
    # command substitutes @VAR@ in the text
    string(JOIN "\n" _list ${_files})
    string(REPLACE "@" "%40" _list "${_list}")
    file(CONFIGURE OUTPUT "${ARG_STAMP}" CONTENT "${_list}\n" @ONLY NEWLINE_STYLE UNIX)
    set(${ARG_OUT} ${_files} "${ARG_STAMP}" PARENT_SCOPE)
endfunction()

function(be_web_generate_shell)
    cmake_parse_arguments(ARG "SINGLE_THREAD" "OUTPUT" "" ${ARGN})
    if(NOT ARG_OUTPUT)
        message(FATAL_ERROR "be_web_generate_shell: OUTPUT is required")
    endif()

    # @ONLY: the file has CSS rules with "@supports", and no other @word@ pair than the placeholder (test_shell.py checks that)
    if(ARG_SINGLE_THREAD)
        set(BAGI_WEB_THREADS 0)
    else()
        set(BAGI_WEB_THREADS 1)
    endif()
    configure_file("${BE_WEB_SHELL_FILE}" "${ARG_OUTPUT}" @ONLY NEWLINE_STYLE UNIX)
endfunction()

function(be_web_link_options)
    cmake_parse_arguments(ARG "SINGLE_THREAD" "OUT;SHELL_FILE" "" ${ARGN})
    if(NOT ARG_OUT)
        message(FATAL_ERROR "be_web_link_options: OUT is required")
    endif()
    if(NOT ARG_SHELL_FILE)
        set(ARG_SHELL_FILE "${BE_WEB_SHELL_FILE}")
    endif()

    # The names are those of settings.js of emsdk 4.0.23 (checked there: each exists, with its default in the comment). The defaults
    # that are right already (GL_ENABLE_GET_PROC_ADDRESS is on, so SDL's GLES2 renderer finds its functions; FILESYSTEM is on; at
    # -O1 and above ASSERTIONS is off) are not repeated.
    set(_options
        # The wasm heap grows on demand (textures and the scene do not fit a fixed size). The limit is the default MAXIMUM_MEMORY, 2 GB
        -sALLOW_MEMORY_GROWTH=1
        # The default stack (STACK_SIZE) is 64 KB, too small for the engine (deep calls, large locals): 5 MB
        -sSTACK_SIZE=5242880
        # WebGL 2 where the browser has it, WebGL 1 as the fallback (the SDL renderer picks the GLES2 driver)
        -sMIN_WEBGL_VERSION=1
        -sMAX_WEBGL_VERSION=2
        # main() never returns while the frame loop runs (src/main.cpp, emscripten_set_main_loop_arg with simulate_infinite_loop); when
        # the loop is cancelled the runtime exits and the static destructors run
        -sEXIT_RUNTIME=1
        # The page around the wasm (web/shell.html); SHELL: keeps the flag and its value together, the quotes keep a path with a space in one piece
        "SHELL:--shell-file \"${ARG_SHELL_FILE}\""
        # Runtime checks of the glue code (memory growth, stack, wrong calls); they cost speed and size, so Debug only
        $<$<CONFIG:Debug>:-sASSERTIONS=1>
    )
    # The optimisation level is not set here: CMake passes CMAKE_CXX_FLAGS_<CONFIG> to the link line as well, and emcc needs the
    # same -O level at link time to run wasm-opt. The end of this file sets Release to -O2 on the web (smaller code than -O3 for
    # little speed) and Debug stays -O0 -g.
    if(NOT ARG_SINGLE_THREAD)
        list(APPEND _options
            # Threads are Web Workers sharing the wasm memory (SharedArrayBuffer: the page must be cross-origin isolated). Every object
            # of the link is compiled with -pthread too (cmake/BagiPlatformOptions.cmake)
            -pthread
            # The workers are created and loaded before main(): see BE_WEB_THREAD_POOL_SIZE. Not PTHREAD_POOL_DELAY_LOAD (the start-up
            # WaitAll would wait for workers that load after main() returns to the event loop, which never happens)
            -sPTHREAD_POOL_SIZE=${BE_WEB_THREAD_POOL_SIZE}
            # Creating a thread when the pool is empty is a hard error (EAGAIN) instead of a silent deadlock of a blocked main thread
            -sPTHREAD_POOL_SIZE_STRICT=2
            # Shared memory with growth needs a fixed maximum (BE_WEB_MAXIMUM_MEMORY)
            -sMAXIMUM_MEMORY=${BE_WEB_MAXIMUM_MEMORY}
            # emcc 4.0.23 warns "-pthread + ALLOW_MEMORY_GROWTH may run non-wasm code slowly" (-Wpthreads-mem-growth): it is about the JS glue
            # that views the heap (each growth replaces the views), not about the wasm code, and a game that needs a heap that grows needs both
            -Wno-pthreads-mem-growth
            # No -sPROXY_TO_PTHREAD: main() and the SDL frame loop stay on the main thread, which owns the canvas
        )
    endif()
    set(${ARG_OUT} "${_options}" PARENT_SCOPE)
endfunction()

function(be_web_configure_target TARGET_NAME)
    cmake_parse_arguments(ARG "" "GAME_ROOT" "" ${ARGN})
    if(NOT ARG_GAME_ROOT)
        set(ARG_GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX "${BE_WEB_OUTPUT_SUFFIX}")

    # The shell is a generated copy of web/shell.html that knows whether the build is threaded (be_web_generate_shell)
    set(_shell "${CMAKE_CURRENT_BINARY_DIR}/web/shell.html")
    set(_single "")
    if(ENGINE_SINGLE_THREAD)
        set(_single SINGLE_THREAD)
    endif()
    be_web_generate_shell(OUTPUT "${_shell}" ${_single})
    be_web_link_options(OUT _link_options SHELL_FILE "${_shell}" ${_single})
    target_link_options(${TARGET_NAME} PRIVATE ${_link_options})
    # The shell is read at link time: a change of the page relinks (the copy changes when the source does, at the next configure,
    # which configure_file() triggers; the source is listed too)
    set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${BE_WEB_SHELL_FILE}" "${_shell}")

    be_web_preload_args(OUT _preload
        ENGINE_ROOT "${BAGI_ENGINE_DIR}"
        GAME_ROOT "${ARG_GAME_ROOT}"
        WEB_ROOT "${BE_WEB_DIR}")
    foreach(_entry IN LISTS _preload)
        # One "--preload-file <spec>" per entry: SHELL: keeps the pair together and stops CMake from dropping the repeated flag
        # as a duplicate; the quotes keep a path with a space in one piece
        string(REPLACE "--preload-file " "" _spec "${_entry}")
        target_link_options(${TARGET_NAME} PRIVATE "SHELL:--preload-file \"${_spec}\"")

        # The .data package is written at link time: an edit, an added or a deleted file in the folder must relink. CONFIGURE_DEPENDS
        # makes the build re-glob, so a new file is seen without a manual configure (and each build pays for the glob)
        be_web_parse_preload_spec(SPEC "${_spec}" FOLDER _folder VIRTUAL _virtual)
        string(MAKE_C_IDENTIFIER "${_virtual}" _id)
        be_web_content_inputs(OUT _content FOLDER "${_folder}"
            STAMP "${CMAKE_CURRENT_BINARY_DIR}/web-content/${TARGET_NAME}${_id}.txt")
        set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS ${_content})
    endforeach()

    # The content is in the virtual file system of the page, not at the paths of this checkout
    target_compile_definitions(${TARGET_NAME} PRIVATE
        "BAGI_ENGINE_ROOT=\"${BE_WEB_ENGINE_ROOT}\""
        "BAGI_GAME_ROOT=\"${BE_WEB_GAME_ROOT}\""
        "BAGI_WEB_ROOT=\"${BE_WEB_WEB_ROOT}\""
    )
endfunction()

# Optimisation of the web configurations (see be_web_link_options). They are plain variables, set where this file is included, so
# they only take effect when it is included before the targets are created. Checked: -O2 -DNDEBUG is on every compile line of
# web-release (compile_commands.json) and on its link line, and Debug is -O0 -g
if(EMSCRIPTEN)
    # The level of the Release build, compile and link (-Oz is smaller, see the size table in tools/web/CLAUDE.md). A cache variable only so
    # that the two can be compared; the plain variables below win over CMAKE_*_FLAGS_RELEASE of the cache
    set(BE_WEB_OPTIMIZATION "-O2" CACHE STRING "Optimisation level of the web Release build: -O2 (default), -Oz, -Os, -O3")
    set(CMAKE_C_FLAGS_RELEASE "${BE_WEB_OPTIMIZATION} -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "${BE_WEB_OPTIMIZATION} -DNDEBUG")

    # The toolchain file defines the WHOLE_ARCHIVE link feature without attributes (desktop CMake does define them). Without
    # OVERRIDE=DEFAULT a module that is linked whole by an executable and also reached as a plain dependency of a game module
    # (Match3Module links BECoreModule) is "Impossible to link target ... already occurred with the feature 'WHOLE_ARCHIVE'"
    set(CMAKE_LINK_LIBRARY_WHOLE_ARCHIVE_ATTRIBUTES LIBRARY_TYPE=STATIC DEDUPLICATION=YES OVERRIDE=DEFAULT)
endif()
