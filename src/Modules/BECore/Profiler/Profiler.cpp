#include "Profiler.h"

#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/Backends/FanOutBackend.h>
#include <BECore/Profiler/ProfilerSettings.h>
#include <BECore/Profiler/Ring/FrameRingBackend.h>
#if BE_PROFILER_TRACY
#include <BECore/Profiler/Backends/TracyProfilerBackend.h>
#endif

#include <cstdlib>
#include <new>

namespace BECore {

    namespace {

        constinit std::atomic<uint32_t> g_nextThreadId{1};
        constinit thread_local uint32_t t_threadId = 0;
        // Copies made with std::malloc and never freed: a reader may hold the old pointer while a thread renames itself
        constinit std::atomic<const char*> g_threadNames[Profiler::kMaxThreads]{};
        // The sort index + 1 of a thread, 0: none (so the zero-initialised table means "none")
        constinit std::atomic<uint32_t> g_threadSortIndex[Profiler::kMaxThreads]{};
        // raw: the profiler cannot profile itself (Threading/CLAUDE.md)
        constinit std::mutex g_backendMutex;
        // One fan-out per distinct primary, made with std::malloc and never freed (a zone may still end on it); under g_backendMutex
        constinit FanOutBackend* g_fanOuts[Profiler::kMaxFanOuts]{};
        constinit uint32_t g_fanOutCount = 0;
        constinit bool g_fanOutTableFullReported = false;

        /// The file Start was asked to write at Finish (main thread only)
        String& TracePath() {
            static String path;
            return path;
        }

    }  // namespace

    IProfilerBackend* Profiler::ResolveEffective(IProfilerBackend* primary, bool ringEnabled) {
        if (!ringEnabled) {
            return primary;
        }
        FrameRingBackend& ring = GetFrameRing();
        if (primary == nullptr || primary == &ring) {
            return &ring;
        }
        for (uint32_t i = 0; i < g_fanOutCount; ++i) {
            if (&g_fanOuts[i]->GetPrimary() == primary) {
                return g_fanOuts[i];
            }
        }
        void* memory = g_fanOutCount < kMaxFanOuts ? std::malloc(sizeof(FanOutBackend)) : nullptr;
        if (memory == nullptr) {
            if (!g_fanOutTableFullReported) {
                g_fanOutTableFullReported = true;
                LOG_WARNING("Profiler: too many different backends ran beside the frame ring; this one runs without it");
            }
            return primary;
        }
        g_fanOuts[g_fanOutCount] = new (memory) FanOutBackend(*primary, ring);
        return g_fanOuts[g_fanOutCount++];
    }

    void Profiler::PublishEffective(IProfilerBackend* effective) {
        _backend.store(effective, std::memory_order_release);
#if BE_PROFILER
        // Only a backend that asks for them gets the events of every lock operation (Tracy's lockables); Chrome and the ring get none
        LockHooks::SetEventBackend(effective != nullptr && effective->WantsLockEvents() ? effective : nullptr);
#endif
    }

    bool Profiler::SetBackend(IProfilerBackend* backend) {
        std::lock_guard lock(g_backendMutex);
        IProfilerBackend* old = _primary.load(std::memory_order_acquire);
        if (old == backend) {
            return true;
        }
        if (backend != nullptr && !backend->Start()) {
            return false;
        }
        _primary.store(backend, std::memory_order_release);
        PublishEffective(ResolveEffective(backend, _ringEnabled.load(std::memory_order_relaxed)));
        if (old != nullptr) {
            old->Stop();
        }
        return true;
    }

    void Profiler::SetRingEnabled(bool enabled) {
        std::lock_guard lock(g_backendMutex);
        if (_ringEnabled.load(std::memory_order_relaxed) == enabled) {
            return;
        }
        if (enabled) {
            GetFrameRing().Start();  // zones that began before the ring was on are not its own
        }
        _ringEnabled.store(enabled, std::memory_order_release);
        PublishEffective(ResolveEffective(_primary.load(std::memory_order_relaxed), enabled));
    }

    void Profiler::FrameMark() {
        if (IProfilerBackend* backend = GetBackend()) {
            backend->FrameMark();
        }
    }

    void Profiler::Counter(const char* name, double value) {
        if (IProfilerBackend* backend = GetBackend()) {
            backend->Counter(name, value);
        }
    }

    void Profiler::SetThreadName(const char* name) {
        const uint32_t threadId = CurrentThreadId();
        if (threadId < kMaxThreads && name != nullptr) {
            const size_t length = std::strlen(name);
            auto* copy = static_cast<char*>(std::malloc(length + 1));
            if (copy != nullptr) {
                std::memcpy(copy, name, length + 1);
                g_threadNames[threadId].store(copy, std::memory_order_release);
            }
        }
        if (IProfilerBackend* backend = GetBackend()) {
            backend->SetThreadName(name);
        }
    }

    void Profiler::SetThreadName(const char* name, uint32_t sortIndex) {
        const uint32_t threadId = CurrentThreadId();
        if (threadId < kMaxThreads && sortIndex < kNoSortIndex) {
            g_threadSortIndex[threadId].store(sortIndex + 1, std::memory_order_release);
        }
        SetThreadName(name);
    }

    uint32_t Profiler::CurrentThreadId() {
        if (t_threadId == 0) {
            t_threadId = g_nextThreadId.fetch_add(1, std::memory_order_relaxed);
        }
        return t_threadId;
    }

    const char* Profiler::GetThreadName(uint32_t threadId) {
        return threadId < kMaxThreads ? g_threadNames[threadId].load(std::memory_order_acquire) : nullptr;
    }

    uint32_t Profiler::GetThreadSortIndex(uint32_t threadId) {
        if (threadId >= kMaxThreads) {
            return kNoSortIndex;
        }
        const uint32_t stored = g_threadSortIndex[threadId].load(std::memory_order_acquire);
        return stored == 0 ? kNoSortIndex : stored - 1;
    }

    ChromeTraceBackend& Profiler::GetChromeTrace() {
        // Never destroyed: zones of other threads may still end on it while the process exits
        alignas(ChromeTraceBackend) static unsigned char storage[sizeof(ChromeTraceBackend)];
        static ChromeTraceBackend* const recorder = new (storage) ChromeTraceBackend();
        return *recorder;
    }

    FrameRingBackend& Profiler::GetFrameRing() {
        // Never destroyed, like the recorder: a zone of another thread may still end on it while the process exits
        alignas(FrameRingBackend) static unsigned char storage[sizeof(FrameRingBackend)];
        static FrameRingBackend* const ring = new (storage) FrameRingBackend();
        return *ring;
    }

    std::expected<void, String> Profiler::Start(const ProfilerSettings& settings) {
        IProfilerBackend* backend = nullptr;
        switch (settings.backend) {
            case ProfilerBackendKind::None:
                break;
            case ProfilerBackendKind::ChromeTrace:
                backend = &GetChromeTrace();
                GetChromeTrace().MarkTraceStart();
                break;
            case ProfilerBackendKind::Tracy:
#if BE_PROFILER_TRACY
                backend = &GetTracyBackend();
#else
                return std::unexpected(String("Tracy is not in this build"));
#endif
                break;
        }
        if (!SetBackend(backend)) {
            return std::unexpected(Format("the {} backend did not start", EnumUtils<ProfilerBackendKind>::ToString(settings.backend)));
        }
        TracePath() = settings.tracePath;
        return {};
    }

    std::expected<void, String> Profiler::Finish() {
        std::expected<void, String> result;
        if (!TracePath().empty()) {
            result = GetChromeTrace().WriteFile(TracePath());
            TracePath().clear();
        }
        SetBackend(nullptr);
#if BE_PROFILER_TRACY
        TracyProfilerBackend::Shutdown();
#endif
        return result;
    }

}  // namespace BECore
