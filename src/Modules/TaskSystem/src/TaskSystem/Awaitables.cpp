#include <TaskSystem/Awaitables.h>
#include <TaskSystem/TaskManager.h>

namespace BECore {

    // =================================================================
    // MainThreadAwaiter
    // =================================================================

    bool MainThreadAwaiter::await_ready() const noexcept {
        // Если уже в главном потоке, не нужно переключаться
        return TaskManager::GetInstance().IsMainThread();
    }

    void MainThreadAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
        auto* scheduler = TaskManager::GetInstance().GetScheduler();
        if (scheduler) {
            scheduler->Schedule([handle]() mutable {
                handle.resume();
            }, TaskPriority::Normal, ThreadType::MainThread);
        }
    }

    // =================================================================
    // BackgroundAwaiter
    // =================================================================

    void BackgroundAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
        auto* scheduler = TaskManager::GetInstance().GetScheduler();
        if (scheduler) {
            scheduler->Schedule([handle]() mutable {
                handle.resume();
            }, priority, ThreadType::Background);
        }
    }

    // =================================================================
    // DelayAwaiter
    // =================================================================

    void DelayAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
        auto* scheduler = TaskManager::GetInstance().GetScheduler();
        if (scheduler) {
            scheduler->ScheduleDelayed([handle]() mutable {
                handle.resume();
            }, delay, TaskPriority::Normal, threadType);
        }
    }

    // =================================================================
    // YieldAwaiter
    // =================================================================

    void YieldAwaiter::await_suspend(std::coroutine_handle<> handle) noexcept {
        auto& taskManager = TaskManager::GetInstance();
        auto* scheduler = taskManager.GetScheduler();
        
        if (scheduler) {
            // Планируем продолжение в том же типе потока
            ThreadType threadType = taskManager.IsMainThread() 
                ? ThreadType::MainThread 
                : ThreadType::Background;
            
            scheduler->Schedule([handle]() mutable {
                handle.resume();
            }, TaskPriority::Low, threadType);
        }
    }

} // namespace BECore
