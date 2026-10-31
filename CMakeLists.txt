cmake_minimum_required(VERSION 3.31)
project(BagiEngine)

# Engine setup (dependencies, options, modules) and the be_* functions that games build on
include(cmake/BagiEngine.cmake)

if(ENGINE_BUILD_TESTS)
    add_subdirectory(tests)
endif()

# Transitional: games still live in src/Modules and share one executable.
add_subdirectory(src/Modules/Chess)
add_subdirectory(src/Modules/Match3)

be_add_game_executable(MyGame
    GAME_ROOT ${CMAKE_CURRENT_SOURCE_DIR}
    MODULES ChessModule Match3Module
)

if(MSVC)
    set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT MyGame)
endif()
