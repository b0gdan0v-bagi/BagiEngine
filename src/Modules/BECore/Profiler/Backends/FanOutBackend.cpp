#include "FanOutBackend.h"

namespace BECore {

    uint64_t FanOutBackend::BeginZone(const ProfileSite& site) {
        const uint64_t token = _primary.BeginZone(site);
        _secondary.BeginZone(site);
        return token;
    }

    void FanOutBackend::EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) {
        _primary.EndZone(token, site, allocs);
        _secondary.EndZone(0, site, allocs);
    }

    void FanOutBackend::RecordZone(const ProfileSite& site, uint64_t beginTicks, uint64_t endTicks) {
        _primary.RecordZone(site, beginTicks, endTicks);
        _secondary.RecordZone(site, beginTicks, endTicks);
    }

    void FanOutBackend::FrameMark() {
        _primary.FrameMark();
        _secondary.FrameMark();
    }

    void FanOutBackend::Counter(const char* name, double value) {
        _primary.Counter(name, value);
        _secondary.Counter(name, value);
    }

    void FanOutBackend::SetThreadName(const char* name) {
        _primary.SetThreadName(name);
        _secondary.SetThreadName(name);
    }

    void FanOutBackend::OnAlloc(void* ptr, size_t size) {
        _primary.OnAlloc(ptr, size);
    }

    void FanOutBackend::OnFree(void* ptr) {
        _primary.OnFree(ptr);
    }

}  // namespace BECore
