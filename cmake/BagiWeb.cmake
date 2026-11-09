# =============================================================================
# Web (Emscripten) build: link options, content package, the executable as an HTML page
# =============================================================================
# Included by cmake/BagiEngine.cmake. The functions below do not depend on the rest of the engine setup (the cmake -P test
# tools/web/tests/check_bagi_web.cmake includes this file alone), so the pure ones can be checked on a desktop without an
# Emscripten SDK. Only be_web_configure_target() touches a target.
#
# Everything about an Emscripten flag or behaviour is a first guess written without emcc at hand, marked
# "unverified: needs emsdk"; the first build with emsdk 4.x (slice 2 of the entry web-build) is where it gets confirmed or fixed.
#
#   be_web_preload_args(OUT <var> ENGINE_ROOT <dir> GAME_ROOT <dir> WEB_ROOT <dir>)
#       The content as a preloaded package: one "--preload-file <dir>@<virtual dir>" entry per config/ and assets/ folder that exists,
#       in the order of the layers (engine, game, web: lowest priority first, as AddContentRoot in src/main.cpp). The virtual roots are
#       BE_WEB_ENGINE_ROOT, BE_WEB_GAME_ROOT and BE_WEB_WEB_ROOT, the values of BAGI_ENGINE_ROOT / BAGI_GAME_ROOT / BAGI_WEB_ROOT.
#   be_web_link_options(OUT <var>)
#       The Emscripten link options: the "-s..." settings, the HTML shell (web/shell.html), and the assertions in Debug only (as a
#       generator expression, so the list is the same for every configuration).
#   be_web_configure_target(<target> GAME_ROOT <dir>)
#       Makes the executable an HTML page with its content package and the virtual content roots as definitions.
include_guard(GLOBAL)

# The page the link produces is <target>.html next to <target>.js, <target>.wasm and <target>.data
set(BE_WEB_OUTPUT_SUFFIX ".html")

# Where the content layers appear in the virtual file system of the page (the engine's config/ and assets/, the game's on top, the web overlay last)
set(BE_WEB_ENGINE_ROOT "/bagi/engine")
set(BE_WEB_GAME_ROOT "/bagi/game")
set(BE_WEB_WEB_ROOT "/bagi/web")

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
                list(APPEND _args "--preload-file ${_root}/${_folder}@${BE_WEB_${_layer}_ROOT}/${_folder}")
            endif()
        endforeach()
    endforeach()
    set(${ARG_OUT} "${_args}" PARENT_SCOPE)
endfunction()

function(be_web_link_options)
    cmake_parse_arguments(ARG "" "OUT" "" ${ARGN})
    if(NOT ARG_OUT)
        message(FATAL_ERROR "be_web_link_options: OUT is required")
    endif()

    # unverified: needs emsdk. The names are those of emsdk 4.x settings.js.
    set(_options
        # The wasm heap grows on demand (textures and the scene do not fit a fixed size)
        -sALLOW_MEMORY_GROWTH=1
        # The default stack of emsdk 3.1.27 and later is 64 KB, too small for the engine (deep calls, large locals): 5 MB
        -sSTACK_SIZE=5242880
        # WebGL 2 where the browser has it, WebGL 1 as the fallback (the SDL renderer picks the GLES2 driver)
        -sMIN_WEBGL_VERSION=1
        -sMAX_WEBGL_VERSION=2
        # main() never returns while the frame loop runs (src/main.cpp); at the end the runtime shuts down and static destructors run
        -sEXIT_RUNTIME=1
        # The page around the wasm (web/shell.html; unverified: needs emsdk); SHELL: keeps the flag and its value together, the quotes keep a path with a space in one piece
        "SHELL:--shell-file \"${BE_WEB_SHELL_FILE}\""
        # Runtime checks of the glue code (memory growth, stack, wrong calls); they cost speed and size, so Debug only
        $<$<CONFIG:Debug>:-sASSERTIONS=1>
    )
    # The optimisation level is not set here: CMake passes CMAKE_CXX_FLAGS_<CONFIG> to the link line as well, and emcc needs the
    # same -O level at link time to run wasm-opt. BagiEngine.cmake sets Release to -O2 on the web (smaller code than -O3 for
    # little speed) and Debug stays -O0 -g (unverified: needs emsdk).
    set(${ARG_OUT} "${_options}" PARENT_SCOPE)
endfunction()

function(be_web_configure_target TARGET_NAME)
    cmake_parse_arguments(ARG "" "GAME_ROOT" "" ${ARGN})
    if(NOT ARG_GAME_ROOT)
        set(ARG_GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR})
    endif()

    set_target_properties(${TARGET_NAME} PROPERTIES SUFFIX "${BE_WEB_OUTPUT_SUFFIX}")

    be_web_link_options(OUT _link_options)
    target_link_options(${TARGET_NAME} PRIVATE ${_link_options})
    # The shell is read at link time: a change of the page relinks
    set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS "${BE_WEB_SHELL_FILE}")

    be_web_preload_args(OUT _preload
        ENGINE_ROOT "${BAGI_ENGINE_DIR}"
        GAME_ROOT "${ARG_GAME_ROOT}"
        WEB_ROOT "${BE_WEB_DIR}")
    foreach(_entry IN LISTS _preload)
        # One "--preload-file <spec>" per entry: SHELL: keeps the pair together and stops CMake from dropping the repeated flag
        # as a duplicate; the quotes keep a path with a space in one piece (unverified: needs emsdk)
        string(REPLACE "--preload-file " "" _spec "${_entry}")
        target_link_options(${TARGET_NAME} PRIVATE "SHELL:--preload-file \"${_spec}\"")

        # The .data package is written at link time: a changed or added file in the folder must relink. CONFIGURE_DEPENDS makes the
        # build re-glob, so a new file is seen without a manual configure
        string(REGEX REPLACE "@.*$" "" _folder "${_spec}")
        file(GLOB_RECURSE _content CONFIGURE_DEPENDS "${_folder}/*")
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
# they only take effect when it is included before the targets are created. unverified: needs emsdk
if(EMSCRIPTEN)
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG")
    set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
endif()
