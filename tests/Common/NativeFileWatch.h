#pragma once

#include <cstdlib>

namespace BECore::Tests {

    /// The environment variable that lets the cases over the real OS watch backend run; the ctest entry `BagiNativeFileWatch` sets it to 1
    inline constexpr const char* kNativeFileWatchVariable = "BAGI_NATIVE_FILEWATCH_TESTS";

    /// Why such a case is skipped in an ordinary run
    inline constexpr const char* kNativeFileWatchSkip = "the real OS watch backend runs only in the ctest entry BagiNativeFileWatch (label native-watch), or with BAGI_NATIVE_FILEWATCH_TESTS=1";

    /**
     * Whether this run asked for the cases over the real OS watch backend (FSEvents, inotify, ReadDirectoryChangesW). They wait for the platform:
     * on a busy machine fseventsd drops events and a case sits out its whole wait, so the everyday run, the shards and the perf run skip them, and
     * they run for a change of the backends or on request (`.claude/rules/tests-module.md`, "Rules")
     */
    inline bool NativeFileWatchRequested() {
        const char* text = std::getenv(kNativeFileWatchVariable);
        return text != nullptr && text[0] == '1' && text[1] == '\0';
    }

}  // namespace BECore::Tests
