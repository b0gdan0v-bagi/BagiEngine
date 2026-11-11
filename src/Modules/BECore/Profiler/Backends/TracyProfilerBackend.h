#pragma once

#include <BECore/Profiler/IProfilerBackend.h>

namespace BECore {

    /**
     * @brief The vendored Tracy client behind IProfilerBackend (ENGINE_PROFILER_TRACY; src/Modules/tracy/).
     *
     * Tracy is built with TRACY_ON_DEMAND (nothing is sent until a viewer connects), TRACY_MANUAL_LIFETIME and TRACY_DELAYED_INIT:
     * its threads start in Start(), the first time the backend is installed, not before main, and end in Shutdown() (Profiler::Finish).
     * A zone's ProfileSite is passed to Tracy as its source location (same layout, checked by static_asserts). Tracy's zone context
     * does not fit the token, so each thread keeps a stack of them (zones nest per thread). Frame marks, counters (plots), thread names
     * and allocations (with call stacks of kAllocCallstackDepth frames) map to Tracy's own.
     */
    class TracyProfilerBackend : public IProfilerBackend {
    public:
        static constexpr int kAllocCallstackDepth = 8;
        static constexpr uint32_t kMaxZoneDepth = 256;  // deeper zones on one thread are not sent

        bool Start() override;
        void Stop() override {}

        uint64_t BeginZone(const ProfileSite& site) override;
        void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) override;
        void FrameMark() override;
        void Counter(const char* name, double value) override;
        void SetThreadName(const char* name) override;
        void OnAlloc(void* ptr, size_t size) override;
        void OnFree(void* ptr) override;

        /// Ends Tracy's threads. Call it when nothing records any more (no zone open on any thread); after it the backend sends nothing
        static void Shutdown();
        /// True between Start and Shutdown
        [[nodiscard]] static bool IsRunning();
    };

    /// The process's Tracy backend (created on first use, never destroyed)
    [[nodiscard]] TracyProfilerBackend& GetTracyBackend();

}  // namespace BECore
