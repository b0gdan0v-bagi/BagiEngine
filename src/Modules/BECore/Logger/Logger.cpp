#include "Logger.h"

#include <TaskSystem/TaskManager.h>

namespace BECore {

    bool Logger::FlushOnMainThread() {
        const TaskManager& tasks = TaskManager::GetInstance();
        if (tasks.IsInitialized() && !tasks.IsMainThread()) {
            return false;
        }

        LogEvent::Flush();
        FlushLogsEvent::Emit();
        return true;
    }

}  // namespace BECore
