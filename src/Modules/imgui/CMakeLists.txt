# Dear ImGui - Graphical UI library (Vendored)
# Bloat-free Immediate Mode Graphical User interface for C++

add_library(imgui STATIC
    imgui.cpp
    imgui_draw.cpp
    imgui_tables.cpp
    imgui_widgets.cpp
    imgui_demo.cpp
    backends/imgui_impl_sdl3.cpp
    backends/imgui_impl_sdlrenderer3.cpp
)

target_include_directories(imgui PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external/SDL/include
)

target_link_libraries(imgui PRIVATE SDL3-shared)

# Disable warnings for external library
if(MSVC)
    target_compile_options(imgui PRIVATE /W0)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(imgui PRIVATE -w)
endif()

set_target_properties(imgui PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    FOLDER "External"
)
