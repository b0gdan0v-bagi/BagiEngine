#pragma once

#include <BECore/Profiler/IProfilerBackend.h>

namespace BECore {

    /**
     * @brief Sends every event to a primary backend and to a secondary one, so the frame ring can run beside Chrome, Tracy or no recorder.
     *
     * Immutable: the pair is fixed at construction and an object is never destroyed or retargeted. A zone ends on the backend it began on
     * (IProfilerBackend), so changing the pair under a running zone would hand the end of a zone to a backend that never saw its begin.
     * Profiler::SetRingEnabled makes one of these per distinct primary instead. The token of a zone is the primary's, the secondary gets
     * 0 back from its own BeginZone and is given 0 at the end (the ring does not use it). Allocation events go to the primary only.
     * Each side is started and stopped by Profiler, not here.
     */
    class FanOutBackend : public IProfilerBackend {
    public:
        FanOutBackend(IProfilerBackend& primary, IProfilerBackend& secondary) : _primary(primary), _secondary(secondary) {}

        uint64_t BeginZone(const ProfileSite& site) override;
        void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) override;
        void FrameMark() override;
        void Counter(const char* name, double value) override;
        void SetThreadName(const char* name) override;
        void OnAlloc(void* ptr, size_t size) override;
        void OnFree(void* ptr) override;

        [[nodiscard]] IProfilerBackend& GetPrimary() const {
            return _primary;
        }

    private:
        IProfilerBackend& _primary;
        IProfilerBackend& _secondary;
    };

}  // namespace BECore
