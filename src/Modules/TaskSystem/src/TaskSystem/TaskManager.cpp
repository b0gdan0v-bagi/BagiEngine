#include <TaskSystem/TaskManager.h>

namespace BECore {

    void TaskManager::Initialize(PassKey<CoreManager>) {
        if (_isInitialized) {
            LOG_WARNING("[TaskManager] Already initialized");
            return;
        }

        _mainThreadId = std::this_thread::get_id();

        // Создаем пул потоков
        size_t numThreads = std::thread::hardware_concurrency();
        if (numThreads < 2) {
            numThreads = 2;
        }

        _threadPool = eastl::make_unique<ThreadPool>(numThreads);

        // Инициализируем планировщик
        _scheduler.Initialize(_threadPool.get());

        LOG_INFO("[TaskManager] Initialized with {} worker threads"_format(numThreads));
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
            _threadPool.reset();
        }

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
        return count;
    }

} // namespace BECore
