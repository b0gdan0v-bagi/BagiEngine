#pragma once

#include <atomic>
#include <chrono>
#include <mutex>

#include <EASTL/string.h>

namespace BECore {

    /**
     * TaskStatistics - статистика и метрики системы задач.
     * 
     * Собирает информацию о:
     * - Количестве выполненных задач
     * - Времени выполнения
     * - Использовании потоков
     * - Размерах очередей
     */
    class TaskStatistics {
    public:
        struct Snapshot {
            uint64_t totalTasksScheduled = 0;
            uint64_t totalTasksCompleted = 0;
            uint64_t totalTasksCancelled = 0;
            uint64_t totalTasksFailed = 0;
            
            uint64_t mainThreadTasksCompleted = 0;
            uint64_t backgroundTasksCompleted = 0;
            
            double averageTaskDurationMs = 0.0;
            double maxTaskDurationMs = 0.0;
            double minTaskDurationMs = 0.0;
            
            size_t currentPendingTasks = 0;
            size_t currentMainThreadQueueSize = 0;
            size_t currentDelayedTaskCount = 0;
            
            size_t workerThreadCount = 0;
        };

        TaskStatistics() = default;

        // =================================================================
        // Recording methods (thread-safe)
        // =================================================================

        void RecordTaskScheduled() {
            _totalTasksScheduled.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordTaskCompleted(bool isMainThread, double durationMs) {
            _totalTasksCompleted.fetch_add(1, std::memory_order_relaxed);
            
            if (isMainThread) {
                _mainThreadTasksCompleted.fetch_add(1, std::memory_order_relaxed);
            } else {
                _backgroundTasksCompleted.fetch_add(1, std::memory_order_relaxed);
            }

            UpdateDurationStats(durationMs);
        }

        void RecordTaskCancelled() {
            _totalTasksCancelled.fetch_add(1, std::memory_order_relaxed);
        }

        void RecordTaskFailed() {
            _totalTasksFailed.fetch_add(1, std::memory_order_relaxed);
        }

        // =================================================================
        // Snapshot methods
        // =================================================================

        Snapshot GetSnapshot() const {
            Snapshot snapshot;
            
            snapshot.totalTasksScheduled = _totalTasksScheduled.load(std::memory_order_relaxed);
            snapshot.totalTasksCompleted = _totalTasksCompleted.load(std::memory_order_relaxed);
            snapshot.totalTasksCancelled = _totalTasksCancelled.load(std::memory_order_relaxed);
            snapshot.totalTasksFailed = _totalTasksFailed.load(std::memory_order_relaxed);
            
            snapshot.mainThreadTasksCompleted = _mainThreadTasksCompleted.load(std::memory_order_relaxed);
            snapshot.backgroundTasksCompleted = _backgroundTasksCompleted.load(std::memory_order_relaxed);

            {
                std::lock_guard lock(_durationMutex);
                snapshot.averageTaskDurationMs = _averageDurationMs;
                snapshot.maxTaskDurationMs = _maxDurationMs;
                snapshot.minTaskDurationMs = _minDurationMs;
            }

            return snapshot;
        }

        void Reset() {
            _totalTasksScheduled.store(0, std::memory_order_relaxed);
            _totalTasksCompleted.store(0, std::memory_order_relaxed);
            _totalTasksCancelled.store(0, std::memory_order_relaxed);
            _totalTasksFailed.store(0, std::memory_order_relaxed);
            _mainThreadTasksCompleted.store(0, std::memory_order_relaxed);
            _backgroundTasksCompleted.store(0, std::memory_order_relaxed);

            std::lock_guard lock(_durationMutex);
            _averageDurationMs = 0.0;
            _maxDurationMs = 0.0;
            _minDurationMs = std::numeric_limits<double>::max();
            _durationCount = 0;
            _totalDurationMs = 0.0;
        }

        // =================================================================
        // Formatted output
        // =================================================================

        eastl::string ToString() const {
            auto snapshot = GetSnapshot();
            return "[TaskStatistics] Scheduled: {}, Completed: {}, Cancelled: {}, Failed: {}, AvgDuration: {:.2f}ms"_format(
                snapshot.totalTasksScheduled,
                snapshot.totalTasksCompleted,
                snapshot.totalTasksCancelled,
                snapshot.totalTasksFailed,
                snapshot.averageTaskDurationMs
            );
        }

    private:
        void UpdateDurationStats(double durationMs) {
            std::lock_guard lock(_durationMutex);
            
            _totalDurationMs += durationMs;
            _durationCount++;
            _averageDurationMs = _totalDurationMs / static_cast<double>(_durationCount);
            
            if (durationMs > _maxDurationMs) {
                _maxDurationMs = durationMs;
            }
            if (durationMs < _minDurationMs) {
                _minDurationMs = durationMs;
            }
        }

        // Atomic counters
        std::atomic<uint64_t> _totalTasksScheduled{0};
        std::atomic<uint64_t> _totalTasksCompleted{0};
        std::atomic<uint64_t> _totalTasksCancelled{0};
        std::atomic<uint64_t> _totalTasksFailed{0};
        std::atomic<uint64_t> _mainThreadTasksCompleted{0};
        std::atomic<uint64_t> _backgroundTasksCompleted{0};

        // Duration stats (protected by mutex)
        mutable std::mutex _durationMutex;
        double _averageDurationMs = 0.0;
        double _maxDurationMs = 0.0;
        double _minDurationMs = std::numeric_limits<double>::max();
        uint64_t _durationCount = 0;
        double _totalDurationMs = 0.0;
    };

} // namespace BECore
