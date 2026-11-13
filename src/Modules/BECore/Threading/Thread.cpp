#include "Thread.h"

namespace BECore {

    namespace Detail {

        static_assert(kNoThreadSortIndex == Profiler::kNoSortIndex, "Thread.h repeats the constant of the profiler to stay lean");

        void NameCurrentThread(const char* name, uint32_t sortIndex) {
            if (sortIndex == kNoThreadSortIndex) {
                BE_PROFILE_THREAD(name);
            } else {
                BE_PROFILE_THREAD_ORDER(name, sortIndex);
            }
            static_cast<void>(name);
            static_cast<void>(sortIndex);
        }

        void ThreadUnavailable() {
            ASSERT(false, "Thread: no thread can start in a single-thread build (BE_SINGLE_THREAD)");
        }

    }  // namespace Detail

}  // namespace BECore
