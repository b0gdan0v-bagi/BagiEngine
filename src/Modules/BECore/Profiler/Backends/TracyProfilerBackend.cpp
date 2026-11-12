// Compiled only with ENGINE_PROFILER_TRACY, with the Tracy definitions of BECore/CMakeLists.txt and without the PCH
#include "TracyProfilerBackend.h"

// clang-format off: what Profiler.h takes from the PCH in every other file has to come before it
#include <EASTL/string.h>
#include <atomic>
#include <expected>
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

    TracyProfilerBackend& GetTracyBackend() {
        alignas(TracyProfilerBackend) static unsigned char storage[sizeof(TracyProfilerBackend)];
        static TracyProfilerBackend* const backend = new (storage) TracyProfilerBackend();
        return *backend;
    }

}  // namespace BECore
