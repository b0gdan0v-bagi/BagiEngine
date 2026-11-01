# nlohmann/json - Header-only JSON library (Vendored, v3.11.3)
# JSON for Modern C++ by Niels Lohmann

add_library(nlohmann_json INTERFACE)

target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# nlohmann/json requires at least C++11
target_compile_features(nlohmann_json INTERFACE cxx_std_11)

# The engine builds without exceptions: a failed access (at(), get<T>() on the wrong type, ...) aborts instead of
# throwing. The define keeps that identical in every translation unit, also when ENGINE_NO_EXCEPTIONS is OFF.
# Use the library only through BECore/Control/Json.h, see BECore/Control/CLAUDE.md.
target_compile_definitions(nlohmann_json INTERFACE JSON_NOEXCEPTION)

# Group in IDE
set_target_properties(nlohmann_json PROPERTIES FOLDER "External")
