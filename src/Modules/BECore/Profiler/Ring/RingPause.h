#pragma once

#include <cstdint>

namespace BECore {

    namespace RingPauseDetail {
        inline constinit thread_local uint32_t t_depth = 0;
    }  // namespace RingPauseDetail

    /**
     * @brief While one is alive on a thread, the frame ring records no zone from that thread (FrameMark is not paused: a frame is the
     * application's, not the thread's). It keeps a profiler view's own work out of the numbers the view shows.
     *
     * No state is kept per zone: scopes nest, so a zone that began before the pause also ends after it (recorded whole) and a zone that
     * began under the pause also ends under it (it was never pushed, so its end pops nothing). Pauses nest. Other recorders (Chrome,
     * Tracy) are not affected.
     */
    class ScopedRingPause {
    public:
        ScopedRingPause() {
            ++RingPauseDetail::t_depth;
        }
        ~ScopedRingPause() {
            --RingPauseDetail::t_depth;
        }

        ScopedRingPause(const ScopedRingPause&) = delete;
        ScopedRingPause& operator=(const ScopedRingPause&) = delete;

        [[nodiscard]] static bool IsActive() {
            return RingPauseDetail::t_depth != 0;
        }
    };

}  // namespace BECore
