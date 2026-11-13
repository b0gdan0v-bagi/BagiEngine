#pragma once

#include <BECore/Profiler/IProfilerBackend.h>

namespace BECore {

    class ChromeTraceBackend;
    class FrameRingBackend;
    struct ProfilerSettings;

    /**
     * @brief The global state of the profiler: the installed backend, the thread ids and names, the trace of a run.
     *
     * Static only and kept in `constinit` globals, not a Singleton or a CoreManager service: zones run on every thread, from the
     * global operator new before main and after CoreManager is gone, and in tools without a window or a CoreManager.
     * BECore/Profiler/CLAUDE.md.
     */
    class Profiler {
    public:
        Profiler() = delete;

        /// The backend the zones go to, nullptr when none (any thread). It is the primary backend, or the frame ring when that is on
        /// and there is no primary, or a fan-out of the two. Callers that mean "what the application chose" read GetPrimaryBackend
        [[nodiscard]] static IProfilerBackend* GetBackend() {
            return _backend.load(std::memory_order_acquire);
        }

        /// The backend that SetBackend installed (the application's choice: none, the Chrome trace, Tracy), without the frame ring
        [[nodiscard]] static IProfilerBackend* GetPrimaryBackend() {
            return _primary.load(std::memory_order_acquire);
        }

        /**
         * @brief Installs @p backend as the primary backend (nullptr: none). Starts it, then stops the one it replaces.
         * With the frame ring on, the ring keeps running beside the new primary.
         * @return False when @p backend did not start; the old backend stays installed then
         */
        static bool SetBackend(IProfilerBackend* backend);

        /**
         * @brief Runs the frame ring (GetFrameRing) beside the primary backend, or stops it. With the ring off GetBackend() is exactly
         * the primary backend, so a closed panel costs nothing.
         *
         * With no primary the ring is the installed backend; with one it is a FanOutBackend of the two. A fan-out is immutable and
         * never destroyed because a zone ends on the backend it began on; Profiler makes one per distinct primary (a small fixed
         * table). When the table is full the ring is left out and one warning says so.
         */
        static void SetRingEnabled(bool enabled);
        [[nodiscard]] static bool IsRingEnabled() {
            return _ringEnabled.load(std::memory_order_acquire);
        }

        /// The frame ring of the process (created on first use, never destroyed); it records only while SetRingEnabled(true)
        [[nodiscard]] static FrameRingBackend& GetFrameRing();

        /// The end of a frame, to the installed backend
        static void FrameMark();
        /// A named value (@p name a literal), to the installed backend
        static void Counter(const char* name, double value);
        /// Names the calling thread for every backend, the ones installed later included (a copy of @p name is kept)
        static void SetThreadName(const char* name);
        /// Names the calling thread and gives it a place in the list of threads (the Chrome trace writes it as `thread_sort_index`: a lower
        /// number is shown higher). The engine keeps the main thread at 0, the task pool at 100 + n and the control threads at 200
        static void SetThreadName(const char* name, uint32_t sortIndex);

        /// A wait on a Mutex / SharedMutex of at least this many microseconds is recorded as a zone `lock wait: <name>` (any thread; relaxed)
        static void SetLockWaitThresholdUs(uint32_t microseconds) {
            _lockWaitThresholdUs.store(microseconds, std::memory_order_relaxed);
        }
        [[nodiscard]] static uint32_t GetLockWaitThresholdUs() {
            return _lockWaitThresholdUs.load(std::memory_order_relaxed);
        }

        /// The calling thread's id in traces: 1, 2, ... in the order threads first ask
        [[nodiscard]] static uint32_t CurrentThreadId();
        /// The name given to thread @p threadId, nullptr when it has none
        [[nodiscard]] static const char* GetThreadName(uint32_t threadId);
        /// The place given to thread @p threadId by SetThreadName(name, sortIndex), kNoSortIndex when it has none
        [[nodiscard]] static uint32_t GetThreadSortIndex(uint32_t threadId);

        /// The Chrome trace recorder of the process (created on first use, never destroyed)
        [[nodiscard]] static ChromeTraceBackend& GetChromeTrace();

        /// Installs the backend of @p settings and remembers its trace file; the reason when the backend could not start
        static std::expected<void, eastl::string> Start(const ProfilerSettings& settings);
        /// Writes the trace file of Start, when it named one, and uninstalls the backend. Call it when every other thread has ended
        static std::expected<void, eastl::string> Finish();

        static constexpr uint32_t kMaxThreads = 256;  // threads beyond it are recorded without a name
        static constexpr uint32_t kNoSortIndex = 0xFFFFFFFFu;
        static constexpr uint32_t kDefaultLockWaitThresholdUs = 50;
        static constexpr uint32_t kMaxFanOuts = 16;  // distinct primary backends that can run beside the ring over the life of the process

    private:
        /// What GetBackend() becomes for @p primary with the ring on or off (the backend mutex is held)
        static IProfilerBackend* ResolveEffective(IProfilerBackend* primary, bool ringEnabled);

        static constinit inline std::atomic<IProfilerBackend*> _backend{nullptr};  // the effective backend
        static constinit inline std::atomic<IProfilerBackend*> _primary{nullptr};
        static constinit inline std::atomic<bool> _ringEnabled{false};
        static constinit inline std::atomic<uint32_t> _lockWaitThresholdUs{kDefaultLockWaitThresholdUs};
    };

}  // namespace BECore
