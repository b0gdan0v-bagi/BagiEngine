#pragma once

#include <BECore/Threading/LockSite.h>
#include <cstddef>

/**
 * @file LockOrder.h
 * @brief The Debug check of the lock order: a lock of a level is taken only while every ordered lock the thread holds has a lower one.
 *
 * Only with BE_LOCK_ORDER_CHECK (asserts on): in any other build there is no member, no thread-local and no call. Locks of level 0
 * (LockSite::level) are not ordered and never recorded. A level is given by the code that owns the lock, `BE_LOCK_SITE_LEVEL("name", 30)`,
 * and only where the nesting was read in the code; the order is the one every path takes, so a violation is a possible deadlock.
 *
 * The holder stack is per thread, fixed in size and allocates nothing. A successful try_lock is recorded and never checked (it cannot
 * wait, so it cannot deadlock); a release removes its own entry wherever it is; a ConditionVariable wait does not touch the stack
 * (the lock stays "held" while the thread waits). SharedMutex counts on both sides, shared and exclusive.
 */

#if BE_LOCK_ORDER_CHECK

namespace BECore::LockOrder {

    /// How many ordered locks one thread may hold at once (a deeper nesting asserts)
    inline constexpr size_t kMaxHeld = 16;

    /// A blocking acquisition is about to begin: asserts (both lock names in the message) when @p site is not above every level held
    void BeforeAcquire(const LockSite& site);
    /// The lock of @p site is now held by the calling thread (a lock or a successful try_lock)
    void Acquired(const LockSite& site);
    /// The lock of @p site is about to be released by the calling thread
    void Released(const LockSite& site) noexcept;
    /// The number of ordered locks the calling thread holds
    [[nodiscard]] size_t HeldCount() noexcept;

}  // namespace BECore::LockOrder

#endif
