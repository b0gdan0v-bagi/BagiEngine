# Vulkan Loader (vendored)

This directory contains the Vulkan Loader runtime and import library for each supported platform.
It removes the dependency on an installed Vulkan SDK when building BagiEngine.

## Directory layout

```
Vulkan-Loader/
  Windows/
    Lib/
      vulkan-1.lib      <- import library (linker input)
    Bin/
      vulkan-1.dll      <- runtime loader (copied next to the executable at build time)
  macOS/
    lib/
      libvulkan.1.dylib <- runtime loader (copied next to the executable at build time)
```

## Where to get the files

### Windows

Install the [Vulkan SDK for Windows](https://vulkan.lunarg.com/sdk/home#windows) (any recent version).
After installation, copy from `%VULKAN_SDK%`:

| Source                         | Destination                          |
|--------------------------------|--------------------------------------|
| `Lib\vulkan-1.lib`             | `Windows\Lib\vulkan-1.lib`           |
| `Bin\vulkan-1.dll`             | `Windows\Bin\vulkan-1.dll`           |

### macOS

Install the [Vulkan SDK for macOS](https://vulkan.lunarg.com/sdk/home#mac) (LunarG).
After installation, copy from `$VULKAN_SDK`:

| Source                             | Destination                        |
|------------------------------------|------------------------------------|
| `lib/libvulkan.1.dylib`            | `macOS/lib/libvulkan.1.dylib`      |

Alternatively, use [MoltenVK](https://github.com/KhronosGroup/MoltenVK) directly.
In that case, copy `MoltenVK/MoltenVK.xcframework/.../libMoltenVK.dylib` and rename or symlink
it to `libvulkan.1.dylib` in `macOS/lib/`.

## Notes

- Commit the binary files to git after placing them here.
- CMake will fail with a descriptive error message if the expected files are missing.
- The DLL/dylib is automatically copied next to the game executable during POST_BUILD.
