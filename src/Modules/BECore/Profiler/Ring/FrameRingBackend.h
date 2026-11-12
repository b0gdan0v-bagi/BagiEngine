#pragma once

#include <BECore/Profiler/IProfilerBackend.h>
#include <BECore/Profiler/ProfilerClock.h>
#include <BECore/Profiler/Ring/RingSnapshot.h>

namespace BECore {

    /**
     * @brief A bounded memory of the most recent zones of every thread, for the editor's profiler panel; no file, no Chrome trace.
     *
     * It stores **completed zones** (begin tick, end tick, site, depth, allocations), written at EndZone, so overwriting the oldest
     * never leaves a begin without its end. Frames are cut at read time by the ticks of FrameMark: frame k is the interval between
     * mark k-1 and mark k, and a zone belongs to the frame its begin tick falls in.
     *
     * A reader never blocks a writer. Each thread owns a ring of slots written with relaxed atomic stores and publishes the count of
     * finished zones with a release store; Snapshot() copies the window, then checks which slots the writer started overwriting
     * meanwhile and drops them (a seqlock without a lock: the writer announces the slot it is about to overwrite before it writes it).
     * Recording does no `new` (std::malloc, as ChromeTraceBackend), so it may run inside the operator new that ENGINE_PROFILER_ALLOC counts.
     * A thread ring is freed only with the backend; the process's one (Profiler::GetFrameRing) is never destroyed.
     */
    class FrameRingBackend : public IProfilerBackend {
    public:
        static constexpr uint32_t kEventsPerThread = 32768;  // 40 bytes a slot: about 1.3 MB a thread, reached only by a thread that records that much
        static constexpr uint32_t kMaxFrames = 512;          // at least FrameStatsCollector::kFrameHistory
        static constexpr uint32_t kMaxThreadBuffers = 256;
        static constexpr uint32_t kMaxZoneDepth = 64;  // a deeper zone is not recorded (and counted in GetDroppedCount)

        /// The clock is a plain function pointer so a test can give it a fake one; the price is one indirect call per zone end
        using NowFn = uint64_t (*)();

        struct Options {
            NowFn now = &ProfilerClock::Now;
            double nanosecondsPerTick = ProfilerClock::ToNanoseconds(1);
        };

        FrameRingBackend();
        explicit FrameRingBackend(const Options& options);
        ~FrameRingBackend() override;

        FrameRingBackend(const FrameRingBackend&) = delete;
        FrameRingBackend& operator=(const FrameRingBackend&) = delete;

        /// Begins a new epoch: zones still open on a thread are forgotten (their ends record nothing), and what the ring holds from before
        /// is not in a Snapshot() any more. The first frame of the epoch begins now, so the time the ring was off is in no frame
        bool Start() override;

        uint64_t BeginZone(const ProfileSite& site) override;
        void EndZone(uint64_t token, const ProfileSite& site, const ZoneAllocs& allocs) override;
        void FrameMark() override;
        void Counter(const char* /*name*/, double /*value*/) override {}
        void SetThreadName(const char* /*name*/) override {}

        /// A copy of the window of every thread and of the frames (any thread; writers go on recording)
        [[nodiscard]] RingSnapshot Snapshot() const;

        /// Zones lost because there were too many threads or a zone was deeper than kMaxZoneDepth
        [[nodiscard]] uint64_t GetDroppedCount() const;

    private:
        struct ThreadBuffer;

        ThreadBuffer* GetThreadBuffer();

        const Options _options;
        const uint64_t _id;  // tells the thread-local cache which ring its buffer belongs to
        std::atomic<uint64_t> _generation{1};
        std::atomic<uint64_t> _dropped{0};

        mutable std::mutex _registryMutex;
        ThreadBuffer* _buffers[kMaxThreadBuffers]{};
        std::atomic<uint32_t> _bufferCount{0};

        // Frame marks: any thread may mark, so writers take a mutex among themselves; readers take nothing
        std::mutex _markMutex;
        mutable uint64_t _markTicks[kMaxFrames]{};  // accessed through std::atomic_ref (mutable: Snapshot() reads it atomically)
        std::atomic<uint64_t> _markReserved{0};
        std::atomic<uint64_t> _markCount{0};

        // The epoch: the ring made, or the last Start. Snapshot() shows no frame or zone from before it. Written by Start, read by Snapshot,
        // and a writer never takes this mutex
        mutable std::mutex _epochMutex;
        uint64_t _epochMark = 0;   // the first mark that ends a frame of the epoch
        uint64_t _epochTicks = 0;  // that frame begins here
    };

}  // namespace BECore
