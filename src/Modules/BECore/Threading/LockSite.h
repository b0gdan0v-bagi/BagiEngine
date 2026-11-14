#pragma once

#include <BECore/Profiler/ProfileSite.h>

/**
 * @file LockSite.h
 * @brief The static name of a lock (the place a Mutex or SharedMutex is made at) and the build gates that decide what a lock keeps.
 *
 * Lean on purpose: <cstdint> and ProfileSite only, so the lock types can be included before the rest of the PCH (Singleton.h).
 *
 * - BE_LOCK_SITES: a lock keeps a pointer to its LockSite. On when the profiler records (the name of a wait zone, the Tracy lockable)
 *   or when asserts are on (the name in a lock order message). Off in a shipped build: Mutex is then std::mutex in size and cost.
 * - BE_LOCK_ORDER_CHECK: the Debug check of the lock order (Threading/LockOrder.h), with asserts on only.
 */

#if BE_PROFILER || ENGINE_ENABLE_ASSERTS
#define BE_LOCK_SITES 1
#else
#define BE_LOCK_SITES 0
#endif

#if ENGINE_ENABLE_ASSERTS
#define BE_LOCK_ORDER_CHECK 1
#else
#define BE_LOCK_ORDER_CHECK 0
#endif

namespace BECore {

    /**
     * @brief The static place of a lock: its name, the zone a long wait on it is recorded as, and its level in the lock order.
     *
     * `wait` is a ProfileSite so a backend keeps its address as the identity of the zone (and the Tracy backend passes it as a source
     * location). Made by BE_LOCK_SITE / BE_LOCK_SITE_LEVEL, never by hand. `level` 0 means "not ordered"; a lock of a level is taken only
     * while every lock the thread holds has a lower one (LockOrder.h).
     */
    struct LockSite {
        ProfileSite wait;
        const char* name;
        uint32_t level;
    };

}  // namespace BECore

/**
 * A reference to a static LockSite, usable wherever an expression is (a default member initialiser, a static member of a template):
 * `Mutex _mutex{BE_LOCK_SITE("Task queue")};`. The name is a string literal: the wait zone is "lock wait: <name>" by concatenation.
 * The lambda keeps the site a function-local `static constexpr`, so a lock can be constant-initialised.
 */
#define BE_LOCK_SITE_LEVEL(name, level)                                                                                                                                                                \
    ([]() constexpr -> const ::BECore::LockSite& {                                                                                                                                                     \
        static constexpr ::BECore::LockSite site{{"lock wait: " name, name, __FILE__, static_cast<uint32_t>(__LINE__), 0}, name, static_cast<uint32_t>(level)};                                        \
        return site;                                                                                                                                                                                   \
    }())

/// A lock that takes part in no order check (level 0)
#define BE_LOCK_SITE(name) BE_LOCK_SITE_LEVEL(name, 0)
