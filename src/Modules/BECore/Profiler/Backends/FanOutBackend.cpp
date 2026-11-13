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

    bool FanOutBackend::WantsLockEvents() const {
        return _primary.WantsLockEvents();
    }

    void* FanOutBackend::AnnounceLock(const ProfileSite& site, LockKind kind) {
        return _primary.AnnounceLock(site, kind);
    }

    void FanOutBackend::TerminateLock(void* context, LockKind kind) {
        _primary.TerminateLock(context, kind);
    }

    bool FanOutBackend::BeforeLock(void* context, LockKind kind) {
        return _primary.BeforeLock(context, kind);
    }

    void FanOutBackend::AfterLock(void* context, LockKind kind) {
        _primary.AfterLock(context, kind);
    }

    void FanOutBackend::BeforeUnlock(void* context, LockKind kind) {
        _primary.BeforeUnlock(context, kind);
    }

    void FanOutBackend::AfterTryLock(void* context, LockKind kind, bool acquired) {
        _primary.AfterTryLock(context, kind, acquired);
    }

    void FanOutBackend::OnAlloc(void* ptr, size_t size) {
        _primary.OnAlloc(ptr, size);
    }

    void FanOutBackend::OnFree(void* ptr) {
        _primary.OnFree(ptr);
    }

}  // namespace BECore
