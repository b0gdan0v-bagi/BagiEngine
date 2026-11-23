#pragma once

#include <BECore/Types/Types.h>

/**
 * @file
 * @brief The version of the engine, `MAJOR.MINOR.PATCH`
 *
 * The three numbers below are written only by `tools/changelog/changelog.py` (run by `todo-merge`), which rewrites exactly those
 * three `#define` lines: keep each on a line of its own. The macros exist so that a game can branch with `#if`.
 */

#define BE_ENGINE_VERSION_MAJOR 0
#define BE_ENGINE_VERSION_MINOR 10
#define BE_ENGINE_VERSION_PATCH 3

#define BE_ENGINE_VERSION_DETAIL_TO_STRING(x) #x
#define BE_ENGINE_VERSION_DETAIL_EXPAND_TO_STRING(x) BE_ENGINE_VERSION_DETAIL_TO_STRING(x)

/// The version as a string literal, `"0.1.0"`
#define BE_ENGINE_VERSION_STRING                                                                                                                                                                       \
    BE_ENGINE_VERSION_DETAIL_EXPAND_TO_STRING(BE_ENGINE_VERSION_MAJOR)                                                                                                                                 \
    "." BE_ENGINE_VERSION_DETAIL_EXPAND_TO_STRING(BE_ENGINE_VERSION_MINOR) "." BE_ENGINE_VERSION_DETAIL_EXPAND_TO_STRING(BE_ENGINE_VERSION_PATCH)

/// True when the engine is at least `major.minor.patch`; usable in `#if`, the arguments are plain numbers
#define BE_ENGINE_VERSION_AT_LEAST(major, minor, patch)                                                                                                                                                \
    (BE_ENGINE_VERSION_MAJOR > (major) || (BE_ENGINE_VERSION_MAJOR == (major) && (BE_ENGINE_VERSION_MINOR > (minor) || (BE_ENGINE_VERSION_MINOR == (minor) && BE_ENGINE_VERSION_PATCH >= (patch)))))

namespace BECore {

    inline constexpr int kEngineVersionMajor = BE_ENGINE_VERSION_MAJOR;
    inline constexpr int kEngineVersionMinor = BE_ENGINE_VERSION_MINOR;
    inline constexpr int kEngineVersionPatch = BE_ENGINE_VERSION_PATCH;

    /// Built from the same macros as the numbers, so the string cannot drift from them
    inline constexpr StringView kEngineVersion = BE_ENGINE_VERSION_STRING;

}  // namespace BECore
