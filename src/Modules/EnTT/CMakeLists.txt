# EnTT - Header-only ECS library (Vendored)
# Entity Component System library by skypjack

add_library(EnTT INTERFACE)

target_include_directories(EnTT INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# EnTT requires at least C++17
target_compile_features(EnTT INTERFACE cxx_std_17)

# Group in IDE
set_target_properties(EnTT PROPERTIES FOLDER "External")
