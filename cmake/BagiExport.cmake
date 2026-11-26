# =============================================================================
# Export manifest of a game
# =============================================================================
# Included by cmake/BagiEngine.cmake. Which files a game needs beside it is known only to the build (the names of its libraries,
# its content roots), so every game executable writes them down for tools/export/ (the format: tools/export/CLAUDE.md).
#
#   be_add_export_manifest(<target> GAME_ROOT <dir> [VULKAN_LOADER <file>])
#       Writes <binary dir>/export/<target>.json at generate time. VULKAN_LOADER is the loader that the build copies next to
#       the executable. A web build (ENGINE_TARGET_WEB) names the target and the content roots only: the page holds the rest.
#       A multi-config generator writes none (one file cannot describe several configurations, and the export builds with the
#       Ninja presets).

function(be_add_export_manifest TARGET_NAME)
    cmake_parse_arguments(EXPORT "" "GAME_ROOT;VULKAN_LOADER" "" ${ARGN})
    if(CMAKE_CONFIGURATION_TYPES)
        return()
    endif()
    get_filename_component(_GAME_ROOT "${EXPORT_GAME_ROOT}" ABSOLUTE)
    set(_MANIFEST "${CMAKE_BINARY_DIR}/export/${TARGET_NAME}.json")
    set(_COMMON
"  \"target\": \"${TARGET_NAME}\",
  \"engineRoot\": \"${BAGI_ENGINE_DIR}\",
  \"gameRoot\": \"${_GAME_ROOT}\"")
    if(ENGINE_TARGET_WEB)
        file(GENERATE OUTPUT "${_MANIFEST}" CONTENT "{\n${_COMMON}\n}\n")
        return()
    endif()

    set(_LIBRARIES "")
    if(NOT ENGINE_STATIC_SDL)
        foreach(_LIBRARY SDL3-shared SDL3_image-shared)
            if(NOT TARGET ${_LIBRARY})
                continue()
            endif()
            # The name the executable asks the loader for: elsewhere than on Windows that is the soname, not the file behind it
            if(WIN32)
                set(_NAME "$<TARGET_FILE_NAME:${_LIBRARY}>")
            else()
                set(_NAME "$<TARGET_SONAME_FILE_NAME:${_LIBRARY}>")
            endif()
            list(APPEND _LIBRARIES "{\"source\": \"$<TARGET_FILE:${_LIBRARY}>\", \"name\": \"${_NAME}\"}")
        endforeach()
    endif()
    if(EXPORT_VULKAN_LOADER)
        get_filename_component(_NAME "${EXPORT_VULKAN_LOADER}" NAME)
        list(APPEND _LIBRARIES "{\"source\": \"${EXPORT_VULKAN_LOADER}\", \"name\": \"${_NAME}\"}")
    endif()
    list(JOIN _LIBRARIES ",\n    " _LIBRARIES)

    be_third_party_notices_file(_NOTICES ${TARGET_NAME})
    file(GENERATE OUTPUT "${_MANIFEST}" CONTENT
"{
${_COMMON},
  \"executable\": \"$<TARGET_FILE:${TARGET_NAME}>\",
  \"notices\": \"${_NOTICES}\",
  \"libraries\": [
    ${_LIBRARIES}
  ]
}
")
endfunction()
