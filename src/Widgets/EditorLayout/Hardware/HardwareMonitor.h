#pragma once

#include <BECore/Threading/ConditionVariable.h>
#include <BECore/Threading/LockGuards.h>
#include <BECore/Threading/Mutex.h>
#include <BECore/Threading/Thread.h>
#include <BECore/Types/Types.h>
#include <Widgets/EditorLayout/Hardware/HardwareTypes.h>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace BECore {

    /// How often the worker reads the hardware
    inline constexpr std::chrono::milliseconds kHardwarePollPeriod{1000};

    /**
     * @brief The policy of one poll: the GPU reader wins when it returns true with at least one value (kind Gpu), else the CPU reader
     * (kind Cpu), else @p out is reset to kind None. A null reader is skipped. The readings are normalized (NormalizeReadings).
     */
    void SampleHardware(IHardwareReader* gpu, IHardwareReader* cpu, HardwareReadings& out);

    /**
     * @brief Polls two readers on a thread of its own and hands the latest readings to the frame without ever making it wait.
     *
     * Why a thread: a driver call can stall (NVML starts in hundreds of milliseconds and answers late on a saturated GPU, an IOHID
     * read takes milliseconds), and one stall on the frame would be one visible hitch of the editor. The readers are touched by that
     * thread only (or by PollOnce, when no thread runs), so they need no lock.
     *
     * The frame calls Fetch() from one thread (the main one): it costs one atomic load when nothing changed and otherwise only
     * try-locks, so it never blocks. The worker reads outside the lock and takes it to compare and store; the version moves only when
     * the readings differ from the published ones.
     *
     * Start / Stop / Fetch / PollOnce are the main thread's. The single-thread build (Thread::kAvailable is false) starts nothing and
     * Fetch stays false. The destructor stops and joins the worker.
     */
    class HardwareMonitor {
    public:
        HardwareMonitor(UniquePtr<IHardwareReader> gpu, UniquePtr<IHardwareReader> cpu);
        ~HardwareMonitor();
        HardwareMonitor(const HardwareMonitor&) = delete;
        HardwareMonitor& operator=(const HardwareMonitor&) = delete;

        /// Starts the worker: the first read at once, then one per @p period. Does nothing when it runs already or threads are not available.
        void Start(std::chrono::milliseconds period = kHardwarePollPeriod);

        /// Wakes the worker and joins it; harmless when it does not run
        void Stop();

        /// Reads on the calling thread and publishes, the worker's body and the test hook; not while the worker runs (the readers are not thread-safe)
        void PollOnce();

        /// Copies the published readings when they changed since the last true answer. False at once when nothing changed, or when the worker holds the lock.
        [[nodiscard]] bool Fetch(HardwareReadings& out);

        /// Moves when a poll found readings that differ from the published ones
        [[nodiscard]] uint32_t Version() const {
            return _version.load(std::memory_order_acquire);
        }

    private:
        void Run(std::chrono::milliseconds period);
        /// Reads outside the lock, stores under it
        void ReadAndPublish();

        UniquePtr<IHardwareReader> _gpu;
        UniquePtr<IHardwareReader> _cpu;

        Mutex _mutex{BE_LOCK_SITE("Hardware readings")};
        ConditionVariable _wake;
        /// Both guarded by _mutex
        HardwareReadings _published;
        bool _stopRequested = false;

        /// Written under _mutex, read without it
        std::atomic<uint32_t> _version{0};
        /// The version of the last readings Fetch handed out; the fetching thread's
        uint32_t _fetchedVersion = 0;
        /// Declared last: it is joined before anything above goes away
        Thread _thread;
    };

}  // namespace BECore
