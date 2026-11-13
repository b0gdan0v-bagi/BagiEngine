#pragma once

#include <BECore/Threading/Mutex.h>
#include <chrono>
#include <condition_variable>

namespace BECore {

    /**
     * @brief std::condition_variable over the engine's Mutex, with the std interface (wait, wait_for, wait_until, notify_one,
     * notify_all, and the predicate overloads).
     *
     * A wait adopts the native mutex of the lock for the duration of the std wait and hands it back afterwards, so the guard
     * (UniqueLock<Mutex>) and everything the lock keeps about the holder stay true while the thread waits: for the lock order check the
     * lock is still held.
     */
    class ConditionVariable {
    public:
        ConditionVariable() = default;
        ConditionVariable(const ConditionVariable&) = delete;
        ConditionVariable& operator=(const ConditionVariable&) = delete;

        void notify_one() noexcept {
            _native.notify_one();
        }

        void notify_all() noexcept {
            _native.notify_all();
        }

        void wait(std::unique_lock<Mutex>& lock) {
            Adopted adopted(lock);
            _native.wait(adopted.native);
        }

        template <typename TPredicate>
        void wait(std::unique_lock<Mutex>& lock, TPredicate predicate) {
            Adopted adopted(lock);
            _native.wait(adopted.native, predicate);
        }

        template <typename TRep, typename TPeriod>
        std::cv_status wait_for(std::unique_lock<Mutex>& lock, const std::chrono::duration<TRep, TPeriod>& timeout) {
            Adopted adopted(lock);
            return _native.wait_for(adopted.native, timeout);
        }

        template <typename TRep, typename TPeriod, typename TPredicate>
        bool wait_for(std::unique_lock<Mutex>& lock, const std::chrono::duration<TRep, TPeriod>& timeout, TPredicate predicate) {
            Adopted adopted(lock);
            return _native.wait_for(adopted.native, timeout, predicate);
        }

        template <typename TClock, typename TDuration>
        std::cv_status wait_until(std::unique_lock<Mutex>& lock, const std::chrono::time_point<TClock, TDuration>& deadline) {
            Adopted adopted(lock);
            return _native.wait_until(adopted.native, deadline);
        }

        template <typename TClock, typename TDuration, typename TPredicate>
        bool wait_until(std::unique_lock<Mutex>& lock, const std::chrono::time_point<TClock, TDuration>& deadline, TPredicate predicate) {
            Adopted adopted(lock);
            return _native.wait_until(adopted.native, deadline, predicate);
        }

    private:
        /// The native mutex of a locked guard, as the std guard the std condition variable wants; released (not unlocked) at the end
        struct Adopted {
            explicit Adopted(std::unique_lock<Mutex>& lock) : native(lock.mutex()->_native, std::adopt_lock) {}

            ~Adopted() {
                native.release();
            }

            Adopted(const Adopted&) = delete;
            Adopted& operator=(const Adopted&) = delete;

            std::unique_lock<std::mutex> native;
        };

        std::condition_variable _native;
    };

}  // namespace BECore
