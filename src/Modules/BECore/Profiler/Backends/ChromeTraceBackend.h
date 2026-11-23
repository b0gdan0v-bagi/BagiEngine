#pragma once

#include <BECore/Profiler/IProfilerBackend.h>
#include <BECore/Types/Types.h>
#include <BECore/Utils/FunctionRef.h>
#include <atomic>
#include <cstdint>
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
        /// WriteFile hands the file a piece of the text once it holds this much
        static constexpr size_t kWriteChunkBytes = 64 * 1024;
        /// The room WriteFile reserves above a chunk: an event is cut off the chunk only after it is complete; a longer one only grows the buffer once
        static constexpr size_t kLongestEventBytes = 2048;

        ChromeTraceBackend();
        ~ChromeTraceBackend() override;

        ChromeTraceBackend(const ChromeTraceBackend&) = delete;
        ChromeTraceBackend& operator=(const ChromeTraceBackend&) = delete;

        uint64_t BeginZone(const ProfileSite& site) override;
        void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) override;
        void RecordZone(const ProfileSite& site, uint64_t beginTicks, uint64_t endTicks) override;
        void FrameMark() override;
        void Counter(const char* name, double value) override;
        void SetThreadName(const char* name) override;

        /// Events recorded before this moment are left out of the trace (a session that starts now). Time 0 of the trace is this moment
        void MarkTraceStart();

        /// The trace as JSON text (main thread; other threads may go on recording)
        [[nodiscard]] String WriteJson() const;
        /// The same text as WriteJson() into the file @p path (created or replaced), formatted into one buffer that is written out in chunks of
        /// kWriteChunkBytes, so what it allocates does not grow with the events; runs in the zone `Trace write`. The reason when it cannot be written
        [[nodiscard]] std::expected<void, String> WriteFile(StringView path) const;

        /// Events lost because a thread's buffer was full or there were too many threads
        [[nodiscard]] uint64_t GetDroppedCount() const;

    private:
        struct Event;
        struct ThreadBuffer;

        ThreadBuffer* GetThreadBuffer();
        /// Appends the whole trace to @p out; @p checkpoint is called with @p out between two events, where the caller may write it out and clear it
        void WriteEvents(String& out, FunctionRef<void(String&)> checkpoint) const;
        void Record(uint8_t type, const void* data, uint64_t a, uint64_t b, uint64_t ticks);

        const uint64_t _id;  // tells the thread-local cache which recorder its buffer belongs to
        std::atomic<uint64_t> _startTicks;
        std::atomic<uint64_t> _dropped{0};
        //-NoStd: the profiler cannot profile itself (Threading/CLAUDE.md)
        mutable std::mutex _registryMutex;
        ThreadBuffer* _buffers[kMaxThreadBuffers]{};
        std::atomic<uint32_t> _bufferCount{0};
    };

}  // namespace BECore
