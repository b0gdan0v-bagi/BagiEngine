# Chess Module CMake Configuration
# Concrete IGame implementation: pure rules + scene components + ChessGame.

get_filename_component(PROJECT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)
get_filename_component(PROJECT_ROOT "${PROJECT_ROOT}" DIRECTORY)

file(GLOB_RECURSE CHESS_MODULE_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(ChessModule STATIC ${CHESS_MODULE_SOURCES})

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" PREFIX "Chess" FILES ${CHESS_MODULE_SOURCES})

set_target_properties(ChessModule PROPERTIES FOLDER "Modules")

# Reuse PCH from BECoreModule.
target_precompile_headers(ChessModule REUSE_FROM BECoreModule)

if(MSVC)
    target_compile_options(ChessModule PRIVATE /FI"BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" OR CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    target_compile_options(ChessModule PRIVATE -include "BECore/pch.h")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(ChessModule PRIVATE -include "BECore/pch.h")
endif()

target_include_directories(ChessModule PUBLIC
    ${PROJECT_ROOT}/src
    ${PROJECT_ROOT}/src/Modules
    ${PROJECT_ROOT}/src/Modules/Chess/src
    ${CMAKE_BINARY_DIR}
)

target_compile_definitions(ChessModule PRIVATE REFLECTION_GENERATED=1)

target_link_libraries(ChessModule PUBLIC
    BECoreModule
    EventsModule
    GamesModule
    SDLModule
)
