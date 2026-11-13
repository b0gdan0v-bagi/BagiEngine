#include <TaskSystem/TaskHandle.h>

namespace BECore::Detail {

    void LiveTasks::Add(TaskHandleBase* handle) {
        LockGuard lock(_mutex);
        _handles.insert(handle);
    }

    void LiveTasks::Remove(TaskHandleBase* handle) {
        LockGuard lock(_mutex);
        _handles.erase(handle);
    }

    void LiveTasks::AbandonAll() {
        eastl::vector<IntrusivePtr<RefCountedAtomic>> selfReferences;

        {
            LockGuard lock(_mutex);
            for (TaskHandleBase* handle : _handles) {
                if (auto selfReference = handle->Abandon()) {
                    selfReferences.push_back(std::move(selfReference));
                }
            }
        }

        // Dropping the last reference destroys the handle, which unregisters under _mutex, so it happens out here.
        selfReferences.clear();
    }

}  // namespace BECore::Detail
