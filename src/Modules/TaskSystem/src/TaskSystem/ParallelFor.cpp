#include <BECore/Memory/ConcurrentBlockPool.h>
#include <BECore/Memory/Immortal.h>
#include <TaskSystem/ParallelFor.h>
#include <new>
#include <thread>

namespace BECore {

    namespace {

        // The chunks of one call are two per thread: the slowest thread then holds up at most half of a thread's share
        constexpr size_t kChunksPerThread = 2;

        /// The state of one call. A helper job can start after the call has returned, so it never lives on the caller's stack: it is counted by the caller and by every helper
        /// job and goes back to an immortal pool (pages that never shrink) with the last reference
        struct SharedState {
            SharedState(FunctionRef<void(size_t, size_t)> bodyRef, size_t countValue, size_t chunkCount) : body(bodyRef), count(countValue), chunks(chunkCount) {}

            std::atomic<uint32_t> references{1};
            std::atomic<size_t> nextChunk{0};
            std::atomic<size_t> finishedChunks{0};
            FunctionRef<void(size_t, size_t)> body;
            size_t count;
            size_t chunks;
        };

        constinit Immortal<ConcurrentBlockPool> gStatePool{"ParallelFor state", sizeof(SharedState), alignof(SharedState)};

        SharedState* CreateState(FunctionRef<void(size_t, size_t)> body, size_t count, size_t chunks) {
            return new (gStatePool.value.Allocate()) SharedState(body, count, chunks);
        }

        void AddReference(SharedState* state) {
            state->references.fetch_add(1, std::memory_order_relaxed);
        }

        void Release(SharedState* state) {
            if (state->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
                state->~SharedState();
                gStatePool.value.Deallocate(state);
            }
        }

        /// One reference of a helper job. Owned by the job's capture, so a job that is dropped without running (a stopped pool) lets go of it too
        class StateRef {
        public:
            explicit StateRef(SharedState* state) : _state(state) {}
            ~StateRef() {
                if (_state != nullptr) {
                    Release(_state);
                }
            }
            StateRef(StateRef&& other) noexcept : _state(std::exchange(other._state, nullptr)) {}
            StateRef& operator=(StateRef&&) = delete;
            StateRef(const StateRef&) = delete;
            StateRef& operator=(const StateRef&) = delete;

            [[nodiscard]] SharedState& Get() const {
                return *_state;
            }

        private:
            SharedState* _state;
        };

        /// Chunk @p chunk of @p chunks over `[0, count)`: the first `count % chunks` chunks hold one index more
        void ChunkRange(size_t count, size_t chunks, size_t chunk, size_t& begin, size_t& end) {
            const size_t base = count / chunks;
            const size_t longer = count % chunks;
            begin = chunk * base + std::min(chunk, longer);
            end = begin + base + (chunk < longer ? 1 : 0);
        }

        void RunChunk(FunctionRef<void(size_t, size_t)> body, size_t begin, size_t end) {
            BE_PROFILE_SCOPE("ParallelFor chunk");
            body(begin, end);
        }

        /// Claims chunks until none is left. The body is called only for a claimed chunk, so a late helper finds the counter spent and leaves the body alone
        void ClaimChunks(SharedState& state) {
            for (;;) {
                const size_t chunk = state.nextChunk.fetch_add(1, std::memory_order_relaxed);
                if (chunk >= state.chunks) {
                    return;
                }
                size_t begin = 0;
                size_t end = 0;
                ChunkRange(state.count, state.chunks, chunk, begin, end);
                RunChunk(state.body, begin, end);
                state.finishedChunks.fetch_add(1, std::memory_order_release);
            }
        }

    }  // namespace

    void ParallelFor(ThreadPool& pool, size_t count, size_t minChunk, FunctionRef<void(size_t begin, size_t end)> body) {
        if (count == 0) {
            return;
        }

        const size_t workers = pool.GetThreadCount();
        const size_t chunks = std::min(count / std::max<size_t>(minChunk, 1), kChunksPerThread * (workers + 1));
        if (pool.IsInline() || workers == 0 || pool.IsStopped() || chunks < 2) {
            RunChunk(body, 0, count);
            return;
        }

        SharedState* state = CreateState(body, count, chunks);
        const size_t helpers = std::min(workers, chunks - 1);
        for (size_t i = 0; i < helpers; ++i) {
            AddReference(state);
            pool.Submit([ref = StateRef(state)] { ClaimChunks(ref.Get()); });
        }

        ClaimChunks(*state);
        // Every chunk is claimed now; the ones not finished run on other threads, so the wait is bounded by the chunk size
        while (state->finishedChunks.load(std::memory_order_acquire) < chunks) {
            std::this_thread::yield();
        }
        Release(state);
    }

}  // namespace BECore
