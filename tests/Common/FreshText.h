#pragma once

#include <BECore/Format/Format.h>
#include <BECore/PoolString/PoolString.h>
#include <BECore/Types/Types.h>
#include <atomic>
#include <cstdint>

namespace BECore::Tests {

    /**
     * "<stem>_<n>": a text that is not in the PoolString table of this process. The table never frees, so a case that pins what the first
     * Intern of a text does, or that a refused text was never interned, needs a text no earlier case interned: a fixed literal holds only
     * in the first run of the case in a process, and `--gtest_repeat` or a case that interns it first breaks it.
     */
    inline String FreshText(StringView stem) {
        static std::atomic<uint64_t> next{0};
        for (;;) {
            String text = Format("{}_{}", stem, next.fetch_add(1));
            if (!PoolString::Find(StringView(text)).has_value()) {
                return text;
            }
        }
    }

}  // namespace BECore::Tests
