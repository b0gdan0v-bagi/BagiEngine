#include <TaskSystem/TaskManager.h>

namespace BECore {

#if defined(BE_WEB_THREAD_POOL_SIZE)
    // The threaded web build starts its Web Workers before main(): a thread beyond the pool is an error, and the main thread blocks at
    // start-up waiting for the workers. One more than the task system needs is the headroom (cmake/BagiWeb.cmake)
    static_assert(BE_WEB_THREAD_POOL_SIZE >= TaskManager::kMaxWorkerThreads + 1, "BE_WEB_THREAD_POOL_SIZE (cmake/BagiWeb.cmake) must be at least TaskManager::kMaxWorkerThreads + 1");
#endif

    void TaskManager::Initialize(PassKey<CoreManager>) {
        if (_isInitialized) {
            LOG_WARNING("[TaskManager] Already initialized");
            return;
        }

        _mainThreadId = std::this_thread::get_id();

#if BE_SINGLE_THREAD
        // No thread at all (the single-thread build, BE_SINGLE_THREAD): background jobs run on the calling thread at submit time,
        // so the start-up WaitAll over the config loads finds them done.
        _threadPool = eastl::make_unique<ThreadPool>(ThreadPool::InlineTag{});

        _scheduler.Initialize(_threadPool.get());

        LOG_INFO("[TaskManager] Initialized in single-thread mode (inline pool, no worker threads)");
#else
        // Создаем пул потоков
        size_t numThreads = std::min<size_t>(std::thread::hardware_concurrency(), kMaxWorkerThreads);
        if (numThreads < 2) {
            numThreads = 2;
        }

        _threadPool = eastl::make_unique<ThreadPool>(numThreads);

        // Инициализируем планировщик
        _scheduler.Initialize(_threadPool.get());

        LOG_INFO("[TaskManager] Initialized with {} worker threads"_format(numThreads));
#endif
        _isInitialized = true;
    }

    void TaskManager::Update(PassKey<CoreManager>) {
        if (!_isInitialized) {
            return;
        }

        // Обрабатываем отложенные задачи
        _scheduler.ProcessDelayedTasks();

        // Обрабатываем задачи главного потока
        _scheduler.ProcessMainThreadTasks();

        // Single-thread build: the continuations of the background jobs that yielded, for at most the frame budget
        _scheduler.ProcessBackgroundSlices(_frameBudget);
    }

    void TaskManager::AdvanceGameTime(PassKey<CoreManager>, double seconds) {
        if (!_isInitialized) {
            return;
        }

        _scheduler.AdvanceGameTime(seconds);
    }

    void TaskManager::Shutdown(PassKey<CoreManager>) {
        if (!_isInitialized) {
            LOG_WARNING("[TaskManager] Not initialized, skipping shutdown");
            return;
        }

        LOG_INFO("[TaskManager] Shutting down...");

        // Останавливаем планировщик
        _scheduler.Shutdown();

        // Останавливаем пул потоков
        if (_threadPool) {
            _threadPool->Shutdown();
        }

        // Nothing resumes coroutines any more, so every task that has not finished is stuck for good: cancel it
        // (Wait() returns) and drop its self-reference (the frame is freed with the last handle).
        Detail::LiveTasks::GetInstance().AbandonAll();

        _threadPool.reset();

        _isInitialized = false;

        LOG_INFO("[TaskManager] Shutdown complete");
    }

    bool TaskManager::IsMainThread() const {
        return std::this_thread::get_id() == _mainThreadId;
    }

    size_t TaskManager::GetWorkerCount() const {
        return _threadPool ? _threadPool->GetThreadCount() : 0;
    }

    size_t TaskManager::GetPendingTaskCount() const {
        size_t count = 0;
        if (_threadPool) {
            count += _threadPool->GetPendingTaskCount();
        }
        count += _scheduler.GetMainThreadQueueSize();
        count += _scheduler.GetDelayedTaskCount();
        count += _scheduler.GetBackgroundSliceCount();
        return count;
    }

}  // namespace BECore
