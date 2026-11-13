#pragma once

#include <EASTL/hash_set.h>

namespace BECore {

    class TaskHandleBase;

    namespace Detail {

        /**
         * LiveTasks - every TaskHandle that currently exists, so TaskManager::Shutdown can find the tasks that
         * will never finish: parked on Delay / SwitchTo*, queued but dropped with the scheduler queues, or never started.
         *
         * Entries are raw pointers. A handle unregisters itself first thing in its destruction (LiveTaskRegistration is
         * its last member), and the registry lock is held while the entries are used, so a registered handle is never
         * seen half-destroyed.
         */
        class LiveTasks : public SingletonAtomic<LiveTasks> {
            friend class SingletonAtomic<LiveTasks>;

        public:
            void Add(TaskHandleBase* handle);
            void Remove(TaskHandleBase* handle);

            /**
             * Cancels every task that has not finished (waiters return, GetResult() reports Cancelled) and drops the
             * self-reference of the ones that were started, so their frames are freed with the last handle.
             * Call it only when nothing resumes coroutines any more (scheduler stopped, pool joined): a task that is
             * still running while it is abandoned would race with the release of its own frame.
             */
            void AbandonAll();

        private:
            LiveTasks() = default;

            // Level 0: handles are destroyed under the locks of the scheduler queues and of a TaskGroup (their Remove takes this one), and
            // AbandonAll takes the completion lock of a handle under this one, so no single order holds on every path
            Mutex _mutex{BE_LOCK_SITE("Live tasks")};
            eastl::hash_set<TaskHandleBase*> _handles;
        };

        /**
         * Keeps a TaskHandle in LiveTasks for its whole life. Declare it as the LAST member of the handle: members are
         * destroyed in reverse order, so the handle leaves the registry before anything AbandonAll() touches is gone.
         */
        class LiveTaskRegistration {
        public:
            explicit LiveTaskRegistration(TaskHandleBase& handle) : _handle(&handle) {
                LiveTasks::GetInstance().Add(_handle);
            }

            ~LiveTaskRegistration() {
                LiveTasks::GetInstance().Remove(_handle);
            }

            LiveTaskRegistration(const LiveTaskRegistration&) = delete;
            LiveTaskRegistration& operator=(const LiveTaskRegistration&) = delete;

        private:
            TaskHandleBase* _handle;
        };

    }  // namespace Detail

}  // namespace BECore
