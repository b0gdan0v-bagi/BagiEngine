#pragma once

#include <cstddef>
#include <cstring>

#if defined(__SANITIZE_ADDRESS__)
#define BE_ARENA_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define BE_ARENA_ASAN 1
#endif
#endif
#ifndef BE_ARENA_ASAN
#define BE_ARENA_ASAN 0
#endif

#if BE_ARENA_ASAN
#include <sanitizer/asan_interface.h>
#endif

namespace BECore::ArenaPoison {

    /// What Debug writes into memory an arena took back, so a read after Reset or Rewind sees it
    constexpr unsigned char kFreedPattern = 0xDD;

    /// True when Poison does anything (Debug fills, ASan marks); the tests of the effect run only then
    constexpr bool kFillsFreed = ENGINE_ENABLE_ASSERTS != 0;
    constexpr bool kMarksFreed = BE_ARENA_ASAN != 0;

    /// Marks [ptr, ptr + size) as not usable: Debug fills the pattern, ASan poisons it. Release: nothing.
    inline void Poison(void* ptr, size_t size) {
#if BE_ARENA_ASAN
        // The range may hold poisoned alignment gaps and a write into them would be reported
        ASAN_UNPOISON_MEMORY_REGION(ptr, size);
#endif
#if ENGINE_ENABLE_ASSERTS
        std::memset(ptr, kFreedPattern, size);
#endif
#if BE_ARENA_ASAN
        ASAN_POISON_MEMORY_REGION(ptr, size);
#endif
        static_cast<void>(ptr);
        static_cast<void>(size);
    }

    /// Makes exactly [ptr, ptr + size) usable again (ASan only)
    inline void Unpoison(void* ptr, size_t size) {
#if BE_ARENA_ASAN
        ASAN_UNPOISON_MEMORY_REGION(ptr, size);
#endif
        static_cast<void>(ptr);
        static_cast<void>(size);
    }

}  // namespace BECore::ArenaPoison
