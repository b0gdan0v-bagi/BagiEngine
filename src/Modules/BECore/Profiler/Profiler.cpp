#include "Profiler.h"

#include <BECore/Profiler/Backends/ChromeTraceBackend.h>
#include <BECore/Profiler/ProfilerSettings.h>
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
        constinit std::mutex g_backendMutex;

        /// The file Start was asked to write at Finish (main thread only)
        eastl::string& TracePath() {
            static eastl::string path;
            return path;
        }

    }  // namespace

    bool Profiler::SetBackend(IProfilerBackend* backend) {
        std::lock_guard lock(g_backendMutex);
        IProfilerBackend* old = _backend.load(std::memory_order_acquire);
        if (old == backend) {
            return true;
        }
        if (backend != nullptr && !backend->Start()) {
            return false;
        }
        _backend.store(backend, std::memory_order_release);
        if (old != nullptr) {
            old->Stop();
        }
        return true;
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

    uint32_t Profiler::CurrentThreadId() {
        if (t_threadId == 0) {
            t_threadId = g_nextThreadId.fetch_add(1, std::memory_order_relaxed);
        }
        return t_threadId;
    }

    const char* Profiler::GetThreadName(uint32_t threadId) {
        return threadId < kMaxThreads ? g_threadNames[threadId].load(std::memory_order_acquire) : nullptr;
    }

    ChromeTraceBackend& Profiler::GetChromeTrace() {
        // Never destroyed: zones of other threads may still end on it while the process exits
        alignas(ChromeTraceBackend) static unsigned char storage[sizeof(ChromeTraceBackend)];
        static ChromeTraceBackend* const recorder = new (storage) ChromeTraceBackend();
        return *recorder;
    }

    std::expected<void, eastl::string> Profiler::Start(const ProfilerSettings& settings) {
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
                return std::unexpected(eastl::string("Tracy is not in this build"));
#endif
                break;
        }
        if (!SetBackend(backend)) {
            return std::unexpected(Format("the {} backend did not start", EnumUtils<ProfilerBackendKind>::ToString(settings.backend)));
        }
        TracePath() = settings.tracePath;
        return {};
    }

    std::expected<void, eastl::string> Profiler::Finish() {
        std::expected<void, eastl::string> result;
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
