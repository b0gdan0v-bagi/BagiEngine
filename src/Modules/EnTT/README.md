# EnTT Module

This module contains a vendored copy of EnTT (Entity Component System library).

## Setup Instructions

The header files need to be downloaded manually from GitHub:

### 1. Download EnTT

1. Download: https://github.com/skypjack/entt/archive/refs/heads/master.zip
2. Extract the ZIP file
3. Copy files to this directory:
   - Copy `entt-master/src/entt/*` to `entt/`
   - Copy `entt-master/LICENSE` to `LICENSE`

### 2. Verify Setup

After copying files, your directory structure should look like:

```
src/Modules/EnTT/
├── CMakeLists.txt
├── README.md (this file)
├── LICENSE
└── entt/
    ├── entt.hpp
    ├── fwd.hpp
    ├── config/
    ├── container/
    ├── core/
    ├── entity/
    ├── graph/
    ├── locator/
    ├── meta/
    ├── platform/
    ├── poly/
    ├── process/
    ├── resource/
    └── signal/
```

## Why Vendored?

EnTT was originally included as a git submodule, but this caused:
- Unnecessary submodule management overhead
- Slower clone times for the repository
- Potential version conflicts with git submodule recursive updates

By vendoring only the header files (EnTT is header-only), we:
- Simplify dependency management
- Speed up repository operations
- Have full control over the library version
- Avoid external dependencies during build

## Usage

Include EnTT headers in your code:

```cpp
#include <entt/entt.hpp>

// Use the registry
entt::registry registry;
```

## License

EnTT is licensed under the MIT License. See the LICENSE file in this directory.

## Requirements

- C++17 or later
