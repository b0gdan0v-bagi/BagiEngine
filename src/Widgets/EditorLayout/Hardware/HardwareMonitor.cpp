#include "HardwareMonitor.h"

namespace BECore {

    namespace {

        constexpr const char* kThreadName = "Hardware stats";

        /// The reader's answer as readings of @p kind; false when it has nothing to show
        bool ReadFrom(IHardwareReader* reader, HardwareKind kind, HardwareReadings& out) {
            out = HardwareReadings{};
            if (reader == nullptr || !reader->Read(out)) {
                return false;
            }
            NormalizeReadings(out);
            if (!HasHardwareValue(out)) {
                return false;
            }
            out.kind = kind;
            return true;
        }

    }  // namespace

    void SampleHardware(IHardwareReader* gpu, IHardwareReader* cpu, HardwareReadings& out) {
        if (ReadFrom(gpu, HardwareKind::Gpu, out) || ReadFrom(cpu, HardwareKind::Cpu, out)) {
            return;
        }
        out = HardwareReadings{};
    }

    HardwareMonitor::HardwareMonitor(UniquePtr<IHardwareReader> gpu, UniquePtr<IHardwareReader> cpu) : _gpu(std::move(gpu)), _cpu(std::move(cpu)) {}

    HardwareMonitor::~HardwareMonitor() {
        Stop();
    }

    void HardwareMonitor::Start(std::chrono::milliseconds period) {
        if (!Thread::kAvailable || _thread.joinable()) {
            return;
        }
        {
            LockGuard lock(_mutex);
            _stopRequested = false;
        }
        _thread = Thread(kThreadName, [this, period] { Run(period); });
    }

    void HardwareMonitor::Stop() {
        if (!_thread.joinable()) {
            return;
        }
        {
            LockGuard lock(_mutex);
            _stopRequested = true;
        }
        _wake.notify_all();
        _thread.join();
    }

    void HardwareMonitor::PollOnce() {
        ASSERT(!_thread.joinable(), "HardwareMonitor::PollOnce while the worker runs: the readers belong to the worker");
        ReadAndPublish();
    }

    bool HardwareMonitor::Fetch(HardwareReadings& out) {
        if (Version() == _fetchedVersion) {
            return false;
        }
        UniqueLock<Mutex> lock(_mutex, std::try_to_lock);
        if (!lock.owns_lock()) {
            return false;
        }
        out = _published;
        _fetchedVersion = _version.load(std::memory_order_relaxed);
        return true;
    }

    void HardwareMonitor::Run(std::chrono::milliseconds period) {
        for (;;) {
            ReadAndPublish();
            UniqueLock<Mutex> lock(_mutex);
            if (_wake.wait_for(lock, period, [this] { return _stopRequested; })) {
                return;
            }
        }
    }

    void HardwareMonitor::ReadAndPublish() {
        HardwareReadings readings;
        SampleHardware(_gpu.get(), _cpu.get(), readings);

        LockGuard lock(_mutex);
        if (readings == _published) {
            return;
        }
        _published = readings;
        _version.fetch_add(1, std::memory_order_release);
    }

}  // namespace BECore
