#pragma once

#include <thread>

namespace BECore {

    namespace Detail {

        /// No place in the list of threads of a trace (the same value as Profiler::kNoSortIndex)
        inline constexpr uint32_t kNoThreadSortIndex = 0xFFFFFFFFu;

        /// Names the calling thread in the profiler (the first thing a Thread does); a no-op with the profiler off (Thread.cpp)
        void NameCurrentThread(const char* name, uint32_t sortIndex);

        /// A Thread was made in a single-thread build: asserts, starts nothing (Thread.cpp)
        void ThreadUnavailable();

        /// What the new thread runs: it names itself, then runs the body. The name is a copy, so a run-time built name may die first.
        template <typename TBody>
        class NamedThreadBody {
        public:
            template <typename TArg>
            NamedThreadBody(const char* name, uint32_t sortIndex, TArg&& body) : _name(name), _sortIndex(sortIndex), _body(std::forward<TArg>(body)) {}

            void operator()() {
                NameCurrentThread(_name.c_str(), _sortIndex);
                _body();
            }

        private:
            String _name;
            uint32_t _sortIndex;
            TBody _body;
        };

    }  // namespace Detail

    /**
     * @brief A thread that has a name: std::thread whose thread calls Profiler::SetThreadName (and the sort index, when given) before
     * the body runs, so no thread of the engine starts without a name in a trace.
     *
     * std-shaped and move-only: joinable, join, detach, get_id. Like std::thread, a joinable Thread that is destroyed or assigned over
     * terminates: join or detach it first. A default-constructed Thread is allowed (a member whose thread starts later) and is not
     * joinable. @p name is copied; the sort index is the place in the list of threads (a lower number is shown higher).
     *
     * The single-thread build (BE_SINGLE_THREAD, the browser's single-thread page) is handled here and nowhere else: kAvailable is
     * false, the constructor asserts and starts nothing, joinable() stays false. The task pool of that build is inline and makes no Thread.
     */
    class Thread {
    public:
        using Id = std::thread::id;

#if BE_SINGLE_THREAD
        static constexpr bool kAvailable = false;
#else
        static constexpr bool kAvailable = true;
#endif

        Thread() noexcept = default;

        template <typename TBody>
        Thread(const char* name, TBody&& body) : Thread(name, Detail::kNoThreadSortIndex, std::forward<TBody>(body)) {}

        template <typename TBody>
        Thread(const char* name, uint32_t sortIndex, TBody&& body) {
#if BE_SINGLE_THREAD
            static_cast<void>(name);
            static_cast<void>(sortIndex);
            static_cast<void>(body);
            Detail::ThreadUnavailable();
#else
            _thread = std::thread(Detail::NamedThreadBody<std::decay_t<TBody>>(name, sortIndex, std::forward<TBody>(body)));
#endif
        }

        Thread(Thread&&) noexcept = default;
        Thread& operator=(Thread&&) noexcept = default;
        Thread(const Thread&) = delete;
        Thread& operator=(const Thread&) = delete;

        [[nodiscard]] bool joinable() const noexcept {
            return _thread.joinable();
        }

        void join() {
            _thread.join();
        }

        void detach() {
            _thread.detach();
        }

        [[nodiscard]] Id get_id() const noexcept {
            return _thread.get_id();
        }

        /// The id of the calling thread
        [[nodiscard]] static Id CurrentId() noexcept {
            return std::this_thread::get_id();
        }

        /// The number of hardware threads, 0 when unknown
        [[nodiscard]] static uint32_t HardwareConcurrency() noexcept {
            return std::thread::hardware_concurrency();
        }

    private:
        std::thread _thread;
    };

}  // namespace BECore
