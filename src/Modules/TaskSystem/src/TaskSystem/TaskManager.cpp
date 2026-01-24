#include <TaskSystem/TaskManager.h>

#include <thread>

namespace BECore {

    void TaskManager::Initialize(PassKey<CoreManager>) {
        if (_isInitialized) {
            LOG_WARNING("[TaskManager] Already initialized");
            return;
        }

        _mainThreadId = std::this_thread::get_id();
        _workerCount = std::thread::hardware_concurrency();
        
        // Минимум 2 потока, даже на одноядерных системах
        if (_workerCount < 2) {
            _workerCount = 2;
        }

        LOG_INFO("[TaskManager] Initialized with {} worker threads"_format(_workerCount));
        _isInitialized = true;
    }

    void TaskManager::Update(PassKey<CoreManager>) {
        if (!_isInitialized) {
            return;
        }

        // TODO: Обработка задач из очереди главного потока
        // Будет реализовано в следующих этапах
    }

    void TaskManager::Shutdown(PassKey<CoreManager>) {
        if (!_isInitialized) {
            LOG_WARNING("[TaskManager] Not initialized, skipping shutdown");
            return;
        }

        LOG_INFO("[TaskManager] Shutting down...");

        // TODO: Остановка пула потоков и ожидание завершения задач
        // Будет реализовано в следующих этапах

        _isInitialized = false;
        _workerCount = 0;

        LOG_INFO("[TaskManager] Shutdown complete");
    }

    bool TaskManager::IsMainThread() const {
        return std::this_thread::get_id() == _mainThreadId;
    }

} // namespace BECore
