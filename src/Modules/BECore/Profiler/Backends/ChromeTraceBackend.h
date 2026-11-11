#pragma once

#include <BECore/Profiler/IProfilerBackend.h>
#include <EASTL/string.h>
#include <EASTL/string_view.h>
#include <atomic>
#include <expected>
#include <mutex>

namespace BECore {

    /**
     * @brief The engine's own recorder: per-thread event buffers written to Chrome trace JSON (ui.perfetto.dev, chrome://tracing).
     *
     * Each thread writes into a buffer of its own without a lock: events go into chunks (std::malloc, so recording never enters the
     * global operator new it may be counting), and the count of finished events is published with a release store. A buffer is
     * registered once under a mutex and freed only with the recorder, so a thread may end at any time. A thread that records more
     * than kMaxEventsPerThread events loses the rest (GetDroppedCount). Writing the trace from the main thread while other threads
     * record is safe: it reads the published part of every buffer, and zones still open at that moment are closed at their thread's
     * last event, so the output is always balanced.
     */
    class ChromeTraceBackend : public IProfilerBackend {
    public:
        static constexpr uint32_t kChunkEvents = 16384;
        static constexpr uint32_t kMaxChunks = 64;
        static constexpr uint32_t kMaxEventsPerThread = kChunkEvents * kMaxChunks;
        static constexpr uint32_t kMaxThreadBuffers = 256;

        ChromeTraceBackend();
        ~ChromeTraceBackend() override;

        ChromeTraceBackend(const ChromeTraceBackend&) = delete;
        ChromeTraceBackend& operator=(const ChromeTraceBackend&) = delete;

        uint64_t BeginZone(const ProfileSite& site) override;
        void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) override;
        void FrameMark() override;
        void Counter(const char* name, double value) override;
        void SetThreadName(const char* name) override;

        /// Events recorded before this moment are left out of the trace (a session that starts now). Time 0 of the trace is this moment
        void MarkTraceStart();

        /// The trace as JSON text (main thread; other threads may go on recording)
        [[nodiscard]] eastl::string WriteJson() const;
        /// WriteJson() into the file @p path (created or replaced); the reason when it cannot be written
        [[nodiscard]] std::expected<void, eastl::string> WriteFile(eastl::string_view path) const;

        /// Events lost because a thread's buffer was full or there were too many threads
        [[nodiscard]] uint64_t GetDroppedCount() const;

    private:
        struct Event;
        struct ThreadBuffer;

        ThreadBuffer* GetThreadBuffer();
        void Record(uint8_t type, const void* data, uint64_t a, uint64_t b);

        const uint64_t _id;  // tells the thread-local cache which recorder its buffer belongs to
        std::atomic<uint64_t> _startTicks;
        std::atomic<uint64_t> _dropped{0};
        mutable std::mutex _registryMutex;
        ThreadBuffer* _buffers[kMaxThreadBuffers]{};
        std::atomic<uint32_t> _bufferCount{0};
    };

}  // namespace BECore
