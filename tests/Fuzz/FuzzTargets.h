#pragma once

#include <span>

namespace BECore::Fuzz {

    /// One fuzz target: untrusted bytes in, properties checked inside, a process that dies (FUZZ_CHECK, a sanitizer, an engine assertion) on a finding
    using FuzzFunction = void (*)(const uint8_t* data, size_t size);

    struct FuzzTarget {
        const char* name;          ///< The suffix of the libFuzzer executable (BagiFuzz<name>) and of the test case
        FuzzFunction function;     ///< Fuzz<name>
        const char* corpusFolder;  ///< The folder of tests/Fuzz/corpus with the seeds of this target
    };

    /// Every target, in a fixed order; the corpus test and the libFuzzer wrappers (cmake/BagiFuzz.cmake) use the same functions
    std::span<const FuzzTarget> FuzzTargetList();

    /// Prints the failed condition and aborts the process: the corpus test and a libFuzzer run both see a crash
    [[noreturn]] void FuzzCheckFailed(const char* condition, const char* file, int line);

    /// A property of a target. Not an engine ASSERT: it must hold in every build, asserts on or off
#define FUZZ_CHECK(condition)                                                                                                                                                                          \
    do {                                                                                                                                                                                               \
        if (!(condition)) {                                                                                                                                                                            \
            ::BECore::Fuzz::FuzzCheckFailed(#condition, __FILE__, __LINE__);                                                                                                                           \
        }                                                                                                                                                                                              \
    } while (false)

    // =============================================================================
    // The targets (one file each, tests/Fuzz/Fuzz<Name>.cpp)
    // =============================================================================

    /// BinaryDeserializer::LoadFromBuffer, then every read over the whole document, then the same buffer again
    void FuzzBinaryDeserializer(const uint8_t* data, size_t size);

}  // namespace BECore::Fuzz
