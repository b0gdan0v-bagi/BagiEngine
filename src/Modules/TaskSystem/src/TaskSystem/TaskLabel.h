#pragma once

#include <BECore/Profiler/ProfileZone.h>

namespace BECore {

    /**
     * @brief The name of a task in a profiler trace: the zone that a job of the pool or of the main thread opens around its slice.
     *
     * Every job on a pool thread already sits in a zone called `Task`; a label adds a zone inside it that says what the job is
     * ("Load config"), so a trace shows what each thread works on. A label is made only with BE_TASK_LABEL (its name must be a string
     * literal, as for BE_PROFILE_SCOPE) and passed to TaskManager::Run / RunDelayed / RunDelayedGameTime / RunOnMainThread or to
     * SwitchToBackground / SwitchToMainThread. A default label is empty: no extra zone. Without the profiler (BE_PROFILER=0)
     * BE_TASK_LABEL is the empty label, so the name costs nothing.
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
