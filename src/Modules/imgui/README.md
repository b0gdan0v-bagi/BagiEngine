# Dear ImGui Module

This module contains a vendored copy of Dear ImGui (Immediate Mode GUI library).

## Setup Instructions

The source files need to be downloaded manually from GitHub:

### 1. Download Dear ImGui

1. Download: https://github.com/ocornut/imgui/archive/refs/heads/docking.zip
2. Extract the ZIP file
3. Copy files to this directory:
   
   **Core files:**
   - Copy `imgui-docking/imgui.cpp` to `imgui.cpp`
   - Copy `imgui-docking/imgui.h` to `imgui.h`
   - Copy `imgui-docking/imgui_draw.cpp` to `imgui_draw.cpp`
   - Copy `imgui-docking/imgui_internal.h` to `imgui_internal.h`
   - Copy `imgui-docking/imgui_tables.cpp` to `imgui_tables.cpp`
   - Copy `imgui-docking/imgui_widgets.cpp` to `imgui_widgets.cpp`
   - Copy `imgui-docking/imgui_demo.cpp` to `imgui_demo.cpp`
   - Copy `imgui-docking/imconfig.h` to `imconfig.h`
   - Copy `imgui-docking/imstb_rectpack.h` to `imstb_rectpack.h`
   - Copy `imgui-docking/imstb_textedit.h` to `imstb_textedit.h`
   - Copy `imgui-docking/imstb_truetype.h` to `imstb_truetype.h`
   
   **SDL3 backends:**
   - Copy `imgui-docking/backends/imgui_impl_sdl3.cpp` to `backends/imgui_impl_sdl3.cpp`
   - Copy `imgui-docking/backends/imgui_impl_sdl3.h` to `backends/imgui_impl_sdl3.h`
   - Copy `imgui-docking/backends/imgui_impl_sdlrenderer3.cpp` to `backends/imgui_impl_sdlrenderer3.cpp`
   - Copy `imgui-docking/backends/imgui_impl_sdlrenderer3.h` to `backends/imgui_impl_sdlrenderer3.h`
   
   **License:**
   - Copy `imgui-docking/LICENSE.txt` to `LICENSE.txt`

### 2. Verify Setup

After copying files, your directory structure should look like:

```
src/Modules/imgui/
├── CMakeLists.txt
├── README.md (this file)
├── LICENSE.txt
├── imgui.cpp
├── imgui.h
├── imgui_draw.cpp
├── imgui_internal.h
├── imgui_tables.cpp
├── imgui_widgets.cpp
├── imgui_demo.cpp
├── imconfig.h
├── imstb_rectpack.h
├── imstb_textedit.h
├── imstb_truetype.h
└── backends/
    ├── imgui_impl_sdl3.cpp
    ├── imgui_impl_sdl3.h
    ├── imgui_impl_sdlrenderer3.cpp
    └── imgui_impl_sdlrenderer3.h
```

## Why Vendored?

Dear ImGui was originally included as a git submodule, but this caused:
- Unnecessary submodule management overhead
- Slower clone times for the repository
- ImGui is designed to be copied into projects

By vendoring the core files and SDL3 backends, we:
- Simplify dependency management
- Speed up repository operations
- Have full control over the library version
- Follow ImGui's recommended integration approach
- Only include the backends we actually use (SDL3)

## Usage

Include ImGui in your code:

```cpp
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_sdlrenderer3.h>

// Initialize ImGui context
ImGui::CreateContext();
ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
ImGui_ImplSDLRenderer3_Init(renderer);
```

## License

Dear ImGui is licensed under the MIT License. See the LICENSE.txt file in this directory.

## Branch

This uses the `docking` branch which includes docking and multi-viewport features.
