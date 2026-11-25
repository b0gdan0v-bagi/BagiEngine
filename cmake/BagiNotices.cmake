# =============================================================================
# Third-party notices of a release build
# =============================================================================
# Included by cmake/BagiEngine.cmake. Depends on nothing else of the engine setup (CI/scripts/tests/test_notices_cmake.py
# includes this file alone into a throwaway project), only on the Python3 package a caller has found.
#
#   be_add_third_party_notices(<target> [GAME_NOTICES <file>])
#       After <target> is linked, CI/scripts/third_party_notices_bundle.py writes the notices of the engine's THIRD_PARTY_NOTICES.md
#       (and of <file>, a game's own table of the same format, its paths relative to the folder of <file>) next to the product:
#         desktop           <the folder of the executable>/THIRD_PARTY_NOTICES.txt
#         macOS app bundle  <X.app>/Contents/Resources/THIRD_PARTY_NOTICES.txt (a target with MACOSX_BUNDLE)
#         web               <the folder of the page>/<target>.notices.txt (tools/web/package.py publishes it)
#       The tables and the script are link dependencies of <target>, so an edit of a table writes the file again; the script writes
#       only when the text changed. Without Python the build gets one warning and no file.
#
#   be_third_party_notices_file(<var> <target>)
#       Sets <var> to the generator expression of the file be_add_third_party_notices writes for <target> (the ctest entry reads it).

get_filename_component(BE_NOTICES_ENGINE_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(BE_NOTICES_SCRIPT "${BE_NOTICES_ENGINE_DIR}/CI/scripts/third_party_notices_bundle.py")
set(BE_NOTICES_FILE_NAME "THIRD_PARTY_NOTICES.txt")

function(be_third_party_notices_file OUT_VAR TARGET_NAME)
    if(ENGINE_TARGET_WEB)
        set(_OUT "$<TARGET_FILE_DIR:${TARGET_NAME}>/${TARGET_NAME}.notices.txt")
    else()
        # TARGET_BUNDLE_CONTENT_DIR is the Contents folder of an .app, and the folder of the file of any other target
        set(_OUT "$<IF:$<BOOL:$<TARGET_PROPERTY:${TARGET_NAME},MACOSX_BUNDLE>>,$<TARGET_BUNDLE_CONTENT_DIR:${TARGET_NAME}>/Resources,$<TARGET_FILE_DIR:${TARGET_NAME}>>/${BE_NOTICES_FILE_NAME}")
    endif()
    set(${OUT_VAR} "${_OUT}" PARENT_SCOPE)
endfunction()

function(be_add_third_party_notices TARGET_NAME)
    cmake_parse_arguments(ARG "" "GAME_NOTICES" "" ${ARGN})

    if(NOT Python3_FOUND)
        message(WARNING "be_add_third_party_notices(${TARGET_NAME}): Python3 not found, the third-party notices file is not written")
        return()
    endif()
    be_third_party_notices_file(_OUT ${TARGET_NAME})

    set(_DEPENDS "${BE_NOTICES_ENGINE_DIR}/THIRD_PARTY_NOTICES.md" "${BE_NOTICES_SCRIPT}")
    set(_GAME_ARGS "")
    if(ARG_GAME_NOTICES)
        get_filename_component(_GAME_NOTICES "${ARG_GAME_NOTICES}" ABSOLUTE)
        get_filename_component(_GAME_ROOT "${_GAME_NOTICES}" DIRECTORY)
        list(APPEND _GAME_ARGS --game-notices "${_GAME_NOTICES}" --game-root "${_GAME_ROOT}")
        list(APPEND _DEPENDS "${_GAME_NOTICES}")
    endif()

    add_custom_command(TARGET ${TARGET_NAME} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} "${BE_NOTICES_SCRIPT}" build --root "${BE_NOTICES_ENGINE_DIR}" --out "${_OUT}" ${_GAME_ARGS}
        COMMENT "Writing the third-party notices of ${TARGET_NAME}"
        VERBATIM)
    set_property(TARGET ${TARGET_NAME} APPEND PROPERTY LINK_DEPENDS ${_DEPENDS})
endfunction()
