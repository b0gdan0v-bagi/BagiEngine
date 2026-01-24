# EA Libraries Module

This module contains vendored copies of Electronic Arts' EABase and EASTL libraries.

## Setup Instructions

The source files need to be downloaded manually from GitHub:

### 1. Download EABase

1. Download: https://github.com/electronicarts/EABase/archive/refs/heads/master.zip
2. Extract the ZIP file
3. Copy files to this directory:
   - Copy `EABase-master/include/Common/EABase/*` to `EABase/include/Common/EABase/`
   - Copy `EABase-master/LICENSE` to `EABase/LICENSE`

### 2. Download EASTL

1. Download: https://github.com/electronicarts/EASTL/archive/refs/heads/master.zip
2. Extract the ZIP file
3. Copy files to this directory:
   - Copy `EASTL-master/include/EASTL/*` to `EASTL/include/EASTL/`
   - Copy all `EASTL-master/source/*.cpp` files to `EASTL/source/`
   - Copy `EASTL-master/LICENSE` to `EASTL/LICENSE`

### 3. Verify Setup

After copying files, your directory structure should look like:

```
src/Modules/EABase/
├── CMakeLists.txt
├── README.md (this file)
├── EABase/
│   ├── LICENSE
│   └── include/
│       └── Common/
│           └── EABase/
│               ├── eabase.h
│               ├── config/
│               └── ... (other headers)
└── EASTL/
    ├── LICENSE
    ├── include/
    │   └── EASTL/
    │       ├── vector.h
    │       ├── string.h
    │       └── ... (other headers)
    └── source/
        ├── assert.cpp
        ├── allocator_eastl.cpp
        └── ... (other .cpp files)
```

## Why Vendored?

These libraries were originally fetched via CMake FetchContent, but this caused:
- Network dependency during CMake configuration
- MAX_PATH issues on Windows due to deep nesting in test packages
- Recursive submodule problems

By vendoring only the essential source and header files, we avoid these issues while keeping the libraries tracked in our repository.

## License

Both EABase and EASTL are licensed under BSD-3-Clause. See the LICENSE files in their respective directories.
