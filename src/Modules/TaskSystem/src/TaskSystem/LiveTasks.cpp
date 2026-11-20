#include <TaskSystem/TaskHandle.h>

namespace BECore::Detail {

    void LiveTasks::Add(TaskHandleBase* handle) {
        LockGuard lock(_mutex);
        handle->_livePrev = nullptr;
        handle->_liveNext = _head;
        if (_head != nullptr) {
            _head->_livePrev = handle;
        }
        _head = handle;
    }

    void LiveTasks::Remove(TaskHandleBase* handle) {
        LockGuard lock(_mutex);
        if (handle->_livePrev != nullptr) {
            handle->_livePrev->_liveNext = handle->_liveNext;
        } else {
            _head = handle->_liveNext;
        }
        if (handle->_liveNext != nullptr) {
            handle->_liveNext->_livePrev = handle->_livePrev;
        }
        handle->_liveNext = nullptr;
        handle->_livePrev = nullptr;
    }

    void LiveTasks::AbandonAll() {
        Vector<IntrusivePtr<RefCountedAtomic>> selfReferences;

        {
            LockGuard lock(_mutex);
            for (TaskHandleBase* handle = _head; handle != nullptr; handle = handle->_liveNext) {
                if (auto selfReference = handle->Abandon()) {
                    selfReferences.push_back(std::move(selfReference));
                }
            }
        }

        // Dropping the last reference destroys the handle, which unregisters under _mutex, so it happens out here.
        selfReferences.clear();
    }

}  // namespace BECore::Detail
