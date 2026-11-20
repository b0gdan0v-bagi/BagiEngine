#include "FrameArena.h"


namespace BECore {

    namespace {

        constinit std::atomic<uint64_t> g_frameNumber{1};

        /// The frame an arena was last used in; 0 before its first use, which has nothing to give back
        constinit thread_local uint64_t t_lastUsedFrame = 0;

    }  // namespace

    LinearArena& FrameArena::Get() {
        constinit static thread_local LinearArena arena("Frame", kChunkSize);
        const uint64_t frame = GetFrameNumber();
        if (t_lastUsedFrame != frame) {
            if (t_lastUsedFrame != 0) {
                arena.Reset();
            }
            t_lastUsedFrame = frame;
        }
        return arena;
    }

    uint64_t FrameArena::GetFrameNumber() {
        return g_frameNumber.load(std::memory_order_acquire);
    }

    void FrameArena::Advance() {
        g_frameNumber.fetch_add(1, std::memory_order_release);
        static_cast<void>(Get());
    }

}  // namespace BECore
