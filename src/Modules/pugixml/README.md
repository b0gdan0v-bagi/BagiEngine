# pugixml Module

This module contains a vendored copy of pugixml (lightweight XML parser).

## Setup Instructions

The source files need to be downloaded manually from GitHub:

### 1. Download pugixml

1. Download: https://github.com/zeux/pugixml/archive/refs/heads/master.zip
2. Extract the ZIP file
3. Copy files to this directory:
   - Copy `pugixml-master/src/pugixml.cpp` to `pugixml.cpp`
   - Copy `pugixml-master/src/pugixml.hpp` to `pugixml.hpp`
   - Copy `pugixml-master/src/pugiconfig.hpp` to `pugiconfig.hpp`
   - Copy `pugixml-master/LICENSE.md` to `LICENSE.md`

### 2. Verify Setup

After copying files, your directory structure should look like:

```
src/Modules/pugixml/
├── CMakeLists.txt
├── README.md (this file)
├── LICENSE.md
├── pugixml.cpp
├── pugixml.hpp
└── pugiconfig.hpp
```

## Why Vendored?

pugixml was originally included as a git submodule, but this caused:
- Unnecessary submodule management overhead
- Slower clone times for the repository
- The library consists of only 3 files, making vendoring trivial

By vendoring the three source files, we:
- Simplify dependency management
- Speed up repository operations
- Have full control over the library version
- Avoid external dependencies during build

## Usage

Include pugixml in your code:

```cpp
#include <pugixml.hpp>

pugi::xml_document doc;
doc.load_file("config.xml");
```

## License

pugixml is licensed under the MIT License. See the LICENSE.md file in this directory.

## Requirements

- C++11 or later
