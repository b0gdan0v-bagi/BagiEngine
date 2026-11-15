#pragma once

#include <BECore/Profiler/ProfileZone.h>

namespace BECore {

    /**
     * @brief The name of a task in a profiler trace: a zone opened inside the `Task` zone around a job's slice.
     *
     * Made only with BE_TASK_LABEL (a string literal, as for BE_PROFILE_SCOPE) and passed as the last argument of
     * TaskManager::Run* or SwitchToBackground / SwitchToMainThread. A default label is empty: no extra zone. Without
     * the profiler BE_TASK_LABEL is the empty label.
     */
    struct TaskLabel {
        const ProfileSite* site = nullptr;
    };

    /// Runs @p body inside the zone of @p label (no zone for an empty label)
    template <typename TBody>
    void RunLabeled(TaskLabel label, TBody&& body) {
        if (label.site != nullptr) {
            const ProfileZone zone(*label.site);
            body();
        } else {
            body();
        }
    }

}  // namespace BECore

#if BE_PROFILER

/// A TaskLabel named @p name (a string literal): `TaskManager::GetInstance().Run(MyTask(), TaskPriority::Normal, ThreadType::Background, BE_TASK_LABEL("My task"))`
#define BE_TASK_LABEL(name)                                                                                                                                                                            \
    ([]() noexcept {                                                                                                                                                                                   \
        static constexpr ::BECore::ProfileSite site{name, name, __FILE__, static_cast<uint32_t>(__LINE__), 0};                                                                                         \
        return ::BECore::TaskLabel{&site};                                                                                                                                                             \
    }())

#else

#define BE_TASK_LABEL(name) (::BECore::TaskLabel{})

#endif
