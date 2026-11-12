#include "FuzzTargets.h"

#include <cstdio>
#include <cstdlib>

namespace BECore::Fuzz {

    std::span<const FuzzTarget> FuzzTargetList() {
        static const FuzzTarget targets[] = {
            {"BinaryDeserializer", &FuzzBinaryDeserializer, "binary"},
        };
        return targets;
    }

    void FuzzCheckFailed(const char* condition, const char* file, int line) {
        std::fprintf(stderr, "FUZZ_CHECK failed: %s (%s:%d)\n", condition, file, line);
        std::fflush(stderr);
        std::abort();
    }

}  // namespace BECore::Fuzz
