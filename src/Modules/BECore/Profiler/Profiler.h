#pragma once

#include <BECore/Profiler/IProfilerBackend.h>
#include <EASTL/string.h>
#include <atomic>
#include <expected>

namespace BECore {

    class ChromeTraceBackend;
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

        /// The installed backend, nullptr when none (any thread)
        [[nodiscard]] static IProfilerBackend* GetBackend() {
            return _backend.load(std::memory_order_acquire);
        }

        /**
         * @brief Installs @p backend (nullptr: none). Starts it, then stops the one it replaces.
         * @return False when @p backend did not start; the old backend stays installed then
         */
        static bool SetBackend(IProfilerBackend* backend);

        /// The end of a frame, to the installed backend
        static void FrameMark();
        /// A named value (@p name a literal), to the installed backend
        static void Counter(const char* name, double value);
        /// Names the calling thread for every backend, the ones installed later included (a copy of @p name is kept)
        static void SetThreadName(const char* name);

        /// The calling thread's id in traces: 1, 2, ... in the order threads first ask
        [[nodiscard]] static uint32_t CurrentThreadId();
        /// The name given to thread @p threadId, nullptr when it has none
        [[nodiscard]] static const char* GetThreadName(uint32_t threadId);

        /// The Chrome trace recorder of the process (created on first use, never destroyed)
        [[nodiscard]] static ChromeTraceBackend& GetChromeTrace();

        /// Installs the backend of @p settings and remembers its trace file; the reason when the backend could not start
        static std::expected<void, eastl::string> Start(const ProfilerSettings& settings);
        /// Writes the trace file of Start, when it named one, and uninstalls the backend. Call it when every other thread has ended
        static std::expected<void, eastl::string> Finish();

        static constexpr uint32_t kMaxThreads = 256;  // threads beyond it are recorded without a name

    private:
        static constinit inline std::atomic<IProfilerBackend*> _backend{nullptr};
    };

}  // namespace BECore
