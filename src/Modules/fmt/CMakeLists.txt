# fmt - Fast formatting library (Vendored)
# https://github.com/fmtlib/fmt

add_library(fmt STATIC
    src/format.cc
    src/os.cc
)

target_include_directories(fmt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Compiled library mode (not header-only)
target_compile_definitions(fmt PUBLIC FMT_COMPILED_LIB)

# Disable warnings for external library
if(MSVC)
    target_compile_options(fmt PRIVATE /W0)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(fmt PRIVATE -w)
endif()

set_target_properties(fmt PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "External"
)
