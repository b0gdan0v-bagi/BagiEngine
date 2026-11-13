# =============================================================================
# Sanitizers on every target and libFuzzer executables over the fuzz targets of tests/Fuzz
# =============================================================================
# Included once by cmake/BagiEngine.cmake before the first target, so the flags reach every target (the vendored libraries too).
#
#   ENGINE_SANITIZE        ""   A ';' list of sanitizers for all code, compile and link, with frame pointers. Clang and GCC only: on Windows
#                               the compiler is clang (preset windows-clang-asan), and with address its runtime DLL is copied next to the
#                               executables at configure.
#                               address    AddressSanitizer (-fsanitize=address)
#                               undefined  UndefinedBehaviorSanitizer with float-cast-overflow (a float to integer conversion out of range;
#                                          not in -fsanitize=undefined) and -fno-sanitize-recover (a finding stops the process)
#                               `thread` and coverage are the entry `build-diagnostic-presets`: any other value is a configure error.
#   ENGINE_FUZZ            OFF  The fuzz presets: address;undefined, the same flags as above.
#   ENGINE_FUZZ_LIBFUZZER  OFF  Also instrument for libFuzzer (-fsanitize=fuzzer-no-link) and define be_add_fuzz_executable(). Needs ENGINE_FUZZ
#                               and a compiler with the libFuzzer runtime: Apple clang has none, Homebrew LLVM has (preset macos-fuzz).
#
# Sets BE_SANITIZED (ON when any sanitizer is on) for the code after it. Asserts and expects are the preset's business: macos-fuzz and
# macos-asan keep them ON, because an assertion that an input can reach is a finding, and macos-fuzz-shipped turns them OFF, the flags
# of a shipped build. cmake/sanitizers/ignorelist.txt holds what vendored code cannot pass; tests/Fuzz/CLAUDE.md says how the fuzz
# targets and the corpus are made.
include_guard(GLOBAL)

set(ENGINE_SANITIZE "" CACHE STRING "Sanitizers on every target, a ';' list of: address, undefined (the presets macos-asan, macos-fuzz)")
option(ENGINE_FUZZ "Build every target with AddressSanitizer and UndefinedBehaviorSanitizer (the fuzz presets); the same as ENGINE_SANITIZE=address;undefined" OFF)
option(ENGINE_FUZZ_LIBFUZZER "Instrument for libFuzzer and build the BagiFuzz<Name> executables (needs ENGINE_FUZZ and a compiler with libFuzzer)" OFF)

if(ENGINE_FUZZ_LIBFUZZER AND NOT ENGINE_FUZZ)
    message(FATAL_ERROR "ENGINE_FUZZ_LIBFUZZER needs ENGINE_FUZZ=ON (the fuzzers run under the sanitizers)")
endif()

set(BE_SANITIZERS ${ENGINE_SANITIZE})
if(ENGINE_FUZZ)
    list(APPEND BE_SANITIZERS address undefined)
endif()
list(REMOVE_DUPLICATES BE_SANITIZERS)

foreach(BE_SANITIZER IN LISTS BE_SANITIZERS)
    if(NOT BE_SANITIZER MATCHES "^(address|undefined)$")
        message(FATAL_ERROR "ENGINE_SANITIZE: unknown sanitizer '${BE_SANITIZER}' (known: address, undefined; thread and coverage are the entry build-diagnostic-presets)")
    endif()
endforeach()

if(BE_SANITIZERS)
    set(BE_SANITIZED ON)
    # Not MSVC: that variable is also true for clang with the MSVC target, which is the compiler of the Windows sanitizer preset
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        message(FATAL_ERROR "Sanitizers (ENGINE_SANITIZE=${BE_SANITIZERS}, ENGINE_FUZZ=${ENGINE_FUZZ}) need Clang or GCC (-fsanitize); this compiler is ${CMAKE_CXX_COMPILER_ID} (cl has no -fsanitize here: on Windows use clang, preset windows-clang-asan)")
    endif()

    set(BE_SANITIZER_FLAGS -fno-omit-frame-pointer)
    if("address" IN_LIST BE_SANITIZERS)
        list(APPEND BE_SANITIZER_FLAGS -fsanitize=address)
    endif()
    if("undefined" IN_LIST BE_SANITIZERS)
        list(APPEND BE_SANITIZER_FLAGS -fsanitize=undefined,float-cast-overflow -fno-sanitize-recover=undefined,float-cast-overflow)
    endif()
    add_compile_options(${BE_SANITIZER_FLAGS})
    # The checks a vendored file cannot pass (cmake/sanitizers/ignorelist.txt); the runtime suppressions do not reach -fno-sanitize-recover.
    # Clang only: GCC has no -fsanitize-ignorelist, so a GCC build gets no ignore list
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        add_compile_options(-fsanitize-ignorelist=${CMAKE_CURRENT_LIST_DIR}/sanitizers/ignorelist.txt)
    endif()
    add_link_options(${BE_SANITIZER_FLAGS})

    # The Windows ASan runtime is a DLL the executables load at start (without it they stop with 0xc0000135): copy it next to them
    if(WIN32 AND "address" IN_LIST BE_SANITIZERS)
        execute_process(COMMAND ${CMAKE_CXX_COMPILER} --print-resource-dir
            OUTPUT_VARIABLE BE_CLANG_RESOURCE_DIR OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        file(TO_CMAKE_PATH "${BE_CLANG_RESOURCE_DIR}" BE_CLANG_RESOURCE_DIR)
        # lib/windows/clang_rt.asan_dynamic-x86_64.dll or lib/x86_64-pc-windows-msvc/clang_rt.asan_dynamic.dll, never the other targets'
        file(GLOB_RECURSE BE_ASAN_DLLS "${BE_CLANG_RESOURCE_DIR}/lib/*asan_dynamic*.dll")
        list(FILTER BE_ASAN_DLLS EXCLUDE REGEX "(i386|aarch64)")
        if(BE_ASAN_DLLS)
            list(GET BE_ASAN_DLLS 0 BE_ASAN_DLL)
        else()
            message(FATAL_ERROR "Sanitizers: the ASan runtime DLL (clang_rt.asan_dynamic*.dll) is not under '${BE_CLANG_RESOURCE_DIR}/lib' (compiler ${CMAKE_CXX_COMPILER}); install the LLVM that ships it")
        endif()
        file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
        file(COPY "${BE_ASAN_DLL}" DESTINATION "${CMAKE_BINARY_DIR}/bin")
        message(STATUS "Sanitizers: ASan runtime ${BE_ASAN_DLL} copied to ${CMAKE_BINARY_DIR}/bin")
    endif()

    if(ENGINE_FUZZ_LIBFUZZER)
        # Coverage instrumentation only; the libFuzzer runtime and its main() are linked into the BagiFuzz<Name> executables alone
        add_compile_options(-fsanitize=fuzzer-no-link)
        message(STATUS "Sanitizers: ${BE_SANITIZERS} (libFuzzer executables BagiFuzz<Name>; compiler ${CMAKE_CXX_COMPILER})")
    else()
        message(STATUS "Sanitizers: ${BE_SANITIZERS} (no libFuzzer; compiler ${CMAKE_CXX_COMPILER})")
    endif()
else()
    set(BE_SANITIZED OFF)
    message(STATUS "Sanitizers: DISABLED")
endif()

# be_add_fuzz_executable(<Name> <Function>)
# The libFuzzer executable BagiFuzz<Name> in the output folder of the build, over the function BECore::Fuzz::<Function> of
# BagiFuzzTargets (tests/Fuzz/FuzzTargets.h): a generated LLVMFuzzerTestOneInput that calls it, and the assertion hook that aborts the run
# when an engine ASSERT / EXPECT is reached by the input. Built like be_add_headless_executable (the PCH of BECoreModule, the engine core
# with WHOLE_ARCHIVE); main() is libFuzzer's.
function(be_add_fuzz_executable NAME FUNCTION)
    if(NOT ENGINE_FUZZ_LIBFUZZER)
        message(FATAL_ERROR "be_add_fuzz_executable(${NAME}) needs ENGINE_FUZZ_LIBFUZZER=ON")
    endif()

    set(TARGET_NAME BagiFuzz${NAME})
    set(WRAPPER_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}.cpp")
    # file(CONFIGURE) leaves the file alone when its text is the same, so a configure does not rebuild the wrapper
    file(CONFIGURE OUTPUT "${WRAPPER_SOURCE}" @ONLY CONTENT
"// Generated by be_add_fuzz_executable() (cmake/BagiFuzz.cmake): libFuzzer's entry over BECore::Fuzz::@FUNCTION@
#include <Fuzz/FuzzAssertAbort.h>
#include <Fuzz/FuzzTargets.h>

extern \"C\" int LLVMFuzzerInitialize(int*, char***) {
    BECore::Fuzz::InstallAssertAbort();
    return 0;
}

extern \"C\" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    BECore::Fuzz::@FUNCTION@(data, size);
    return 0;
}
")

    add_executable(${TARGET_NAME} "${WRAPPER_SOURCE}")
    set_target_properties(${TARGET_NAME} PROPERTIES FOLDER "Tests")

    target_precompile_headers(${TARGET_NAME} REUSE_FROM BECoreModule)
    be_force_include_pch(${TARGET_NAME})

    # IMGUI_VULKAN_AVAILABLE must match BECoreModule, otherwise the reused PCH is rejected
    target_compile_definitions(${TARGET_NAME} PRIVATE IMGUI_VULKAN_AVAILABLE=1 REFLECTION_GENERATED=1)
    target_include_directories(${TARGET_NAME} PRIVATE ${BAGI_ENGINE_DIR}/src ${CMAKE_BINARY_DIR})

    be_link_engine(${TARGET_NAME})
    target_link_libraries(${TARGET_NAME} PRIVATE BagiFuzzTargets)
    target_link_options(${TARGET_NAME} PRIVATE -fsanitize=fuzzer)

    if(APPLE)
        set_target_properties(${TARGET_NAME} PROPERTIES
            BUILD_RPATH "@executable_path"
            INSTALL_RPATH "@executable_path"
        )
    endif()

    # BagiFuzzers builds every fuzzer without the rest of the tree
    if(NOT TARGET BagiFuzzers)
        add_custom_target(BagiFuzzers)
        set_target_properties(BagiFuzzers PROPERTIES FOLDER "Tests")
    endif()
    add_dependencies(BagiFuzzers ${TARGET_NAME})
endfunction()
