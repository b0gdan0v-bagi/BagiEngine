// Compiled only with ENGINE_PROFILER_TRACY, with the Tracy definitions of BECore/CMakeLists.txt and without the PCH
#include "TracyProfilerBackend.h"

// clang-format off: what Profiler.h takes from the PCH in every other file has to come before it
#include <BECore/Profiler/Profiler.h>
// clang-format on
#include <cstddef>
#include <new>
#include <tracy/TracyC.h>

namespace BECore {

    // A ProfileSite is handed to Tracy as its source location: the layout must stay the same
    static_assert(sizeof(ProfileSite) == sizeof(___tracy_source_location_data));
    static_assert(alignof(ProfileSite) == alignof(___tracy_source_location_data));
    static_assert(offsetof(ProfileSite, name) == offsetof(___tracy_source_location_data, name));
    static_assert(offsetof(ProfileSite, function) == offsetof(___tracy_source_location_data, function));
    static_assert(offsetof(ProfileSite, file) == offsetof(___tracy_source_location_data, file));
    static_assert(offsetof(ProfileSite, line) == offsetof(___tracy_source_location_data, line));
    static_assert(offsetof(ProfileSite, color) == offsetof(___tracy_source_location_data, color));

    namespace {

        constinit std::atomic<bool> g_running{false};
        constinit std::atomic<bool> g_everStarted{false};

        constexpr uint64_t kNoZone = ~uint64_t{0};

        struct ZoneStack {
            TracyCZoneCtx contexts[TracyProfilerBackend::kMaxZoneDepth];
            uint32_t depth = 0;
        };
        constinit thread_local ZoneStack t_zones{};

        const ___tracy_source_location_data* AsSourceLocation(const ProfileSite& site) {
            return reinterpret_cast<const ___tracy_source_location_data*>(&site);
        }

    }  // namespace

    bool TracyProfilerBackend::Start() {
        // Tracy cannot start twice in one process (TRACY_MANUAL_LIFETIME); a backend installed again keeps the running client
        bool expected = false;
        if (g_everStarted.compare_exchange_strong(expected, true)) {
            ___tracy_startup_profiler();
            g_running.store(true, std::memory_order_release);
            // The thread that starts it (Main) was named before Tracy ran
            if (const char* name = Profiler::GetThreadName(Profiler::CurrentThreadId())) {
                ___tracy_set_thread_name(name);
            }
        }
        return g_running.load(std::memory_order_acquire);
    }

    void TracyProfilerBackend::Shutdown() {
        if (g_running.exchange(false, std::memory_order_acq_rel)) {
            ___tracy_shutdown_profiler();
        }
    }

    bool TracyProfilerBackend::IsRunning() {
        return g_running.load(std::memory_order_acquire);
    }

    uint64_t TracyProfilerBackend::BeginZone(const ProfileSite& site) {
        ZoneStack& zones = t_zones;
        if (!IsRunning() || zones.depth >= kMaxZoneDepth) {
            return kNoZone;
        }
        zones.contexts[zones.depth] = ___tracy_emit_zone_begin(AsSourceLocation(site), 1);
        return zones.depth++;
    }

    void TracyProfilerBackend::EndZone(uint64_t token, const ProfileSite& /*site*/, const ZoneAllocs& /*allocs*/) {
        ZoneStack& zones = t_zones;
        if (token == kNoZone || zones.depth == 0 || token != zones.depth - 1) {
            return;
        }
        --zones.depth;
        if (IsRunning()) {
            ___tracy_emit_zone_end(zones.contexts[zones.depth]);
        }
    }

    void TracyProfilerBackend::FrameMark() {
        if (IsRunning()) {
            ___tracy_emit_frame_mark(nullptr);
        }
    }

    void TracyProfilerBackend::Counter(const char* name, double value) {
        if (IsRunning()) {
            ___tracy_emit_plot(name, value);
        }
    }

    void TracyProfilerBackend::SetThreadName(const char* name) {
        if (IsRunning()) {
            ___tracy_set_thread_name(name);
        }
    }

    void TracyProfilerBackend::OnAlloc(void* ptr, size_t size) {
        if (IsRunning()) {
            ___tracy_emit_memory_alloc_callstack(ptr, size, kAllocCallstackDepth);
        }
    }

    void TracyProfilerBackend::OnFree(void* ptr) {
        if (IsRunning()) {
            ___tracy_emit_memory_free_callstack(ptr, kAllocCallstackDepth);
        }
    }

    namespace {

        TracyCLockCtx AsLock(void* context) {
            return static_cast<TracyCLockCtx>(context);
        }

        TracyCSharedLockCtx AsSharedLock(void* context) {
            return static_cast<TracyCSharedLockCtx>(context);
        }

    }  // namespace

    void* TracyProfilerBackend::AnnounceLock(const ProfileSite& site, LockKind kind) {
        if (!IsRunning()) {
            return nullptr;
        }
        // The lock's name is the `function` of its site (BE_LOCK_SITE); the custom name makes sure the viewer shows it as the lock's label
        const size_t nameLength = std::strlen(site.function);
        if (kind == LockKind::Mutex) {
            TracyCLockCtx context = ___tracy_announce_lockable_ctx(AsSourceLocation(site));
            ___tracy_custom_name_lockable_ctx(context, site.function, nameLength);
            return context;
        }
        TracyCSharedLockCtx context = ___tracy_announce_shared_lockable_ctx(AsSourceLocation(site));
        ___tracy_custom_name_shared_lockable_ctx(context, site.function, nameLength);
        return context;
    }

    void TracyProfilerBackend::TerminateLock(void* context, LockKind kind) {
        if (!IsRunning() || context == nullptr) {
            return;
        }
        if (kind == LockKind::Mutex) {
            ___tracy_terminate_lockable_ctx(AsLock(context));
        } else {
            ___tracy_terminate_shared_lockable_ctx(AsSharedLock(context));
        }
    }

    bool TracyProfilerBackend::BeforeLock(void* context, LockKind kind) {
        if (!IsRunning() || context == nullptr) {
            return false;
        }
        switch (kind) {
            case LockKind::Mutex:
                return ___tracy_before_lock_lockable_ctx(AsLock(context)) != 0;
            case LockKind::SharedWriter:
                return ___tracy_before_lock_shared_lockable_ctx(AsSharedLock(context)) != 0;
            case LockKind::SharedReader:
                return ___tracy_before_lock_shared_shared_lockable_ctx(AsSharedLock(context)) != 0;
        }
        return false;
    }

    void TracyProfilerBackend::AfterLock(void* context, LockKind kind) {
        if (!IsRunning() || context == nullptr) {
            return;
        }
        switch (kind) {
            case LockKind::Mutex:
                ___tracy_after_lock_lockable_ctx(AsLock(context));
                break;
            case LockKind::SharedWriter:
                ___tracy_after_lock_shared_lockable_ctx(AsSharedLock(context));
                break;
            case LockKind::SharedReader:
                ___tracy_after_lock_shared_shared_lockable_ctx(AsSharedLock(context));
                break;
        }
    }

    void TracyProfilerBackend::BeforeUnlock(void* context, LockKind kind) {
        if (!IsRunning() || context == nullptr) {
            return;
        }
        switch (kind) {
            case LockKind::Mutex:
                ___tracy_before_unlock_lockable_ctx(AsLock(context));
                break;
            case LockKind::SharedWriter:
                ___tracy_before_unlock_shared_lockable_ctx(AsSharedLock(context));
                break;
            case LockKind::SharedReader:
                ___tracy_before_unlock_shared_shared_lockable_ctx(AsSharedLock(context));
                break;
        }
    }

    void TracyProfilerBackend::AfterTryLock(void* context, LockKind kind, bool acquired) {
        if (!IsRunning() || context == nullptr) {
            return;
        }
        const int32_t result = acquired ? 1 : 0;
        switch (kind) {
            case LockKind::Mutex:
                ___tracy_after_try_lock_lockable_ctx(AsLock(context), result);
                break;
            case LockKind::SharedWriter:
                ___tracy_after_try_lock_shared_lockable_ctx(AsSharedLock(context), result);
                break;
            case LockKind::SharedReader:
                ___tracy_after_try_lock_shared_shared_lockable_ctx(AsSharedLock(context), result);
                break;
        }
    }

    TracyProfilerBackend& GetTracyBackend() {
        alignas(TracyProfilerBackend) static unsigned char storage[sizeof(TracyProfilerBackend)];
        static TracyProfilerBackend* const backend = new (storage) TracyProfilerBackend();
        return *backend;
    }

}  // namespace BECore
