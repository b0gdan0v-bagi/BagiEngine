#pragma once

#include <BECore/PoolString/PoolBlockSource.h>
#include <BECore/PoolString/PoolStringStorage.h>
#include <BECore/Types/Types.h>
#include <Common/FreshText.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <thread>

namespace BECore::Tests {

    /// A block source over the heap that counts what it is asked for and, with a limit, serves that many allocations and
    /// then answers every call with an empty `PoolMemory` (what a used-up reservation does). A test builds its own
    /// `PoolStringStorage` over one and compares absolute numbers.
    class CountingBlockSource final : public IPoolBlockSource {
        UniquePtr<IPoolBlockSource> _heap = MakeHeapBlockSource();
        size_t _limit;

    public:
        size_t served = 0;       ///< Allocations answered with memory
        size_t refused = 0;      ///< Allocations answered with an empty `PoolMemory`
        size_t servedBytes = 0;  ///< Their total size

        explicit CountingBlockSource(size_t limit = SIZE_MAX) : _limit(limit) {}

        PoolMemory Allocate(size_t bytes) override {
            if (served >= _limit) {
                ++refused;
                return {};
            }
            ++served;
            servedBytes += bytes;
            return _heap->Allocate(bytes);
        }
    };

    /// A storage over a counting source with a small table; the numbers of its statistics are absolute
    struct IsolatedPool {
        static constexpr size_t kSmallTable = 8;

        CountingBlockSource source;
        Details::PoolStringStorage storage;

        explicit IsolatedPool(size_t sourceLimit = SIZE_MAX, size_t tableCapacity = kSmallTable) : source(sourceLimit), storage(source, tableCapacity) {}
    };

    /// The smallest capacity, `start` doubled as often as needed, that holds `used` entries at half load
    inline size_t TableCapacityFor(size_t start, size_t used) {
        size_t capacity = start;
        while (used * 2 > capacity) {
            capacity *= 2;
        }
        return capacity;
    }

    inline String CounterName(StringView prefix, size_t index) {
        String name(prefix);
        name += ToString(index);
        return name;
    }

    /// A text of `length` characters that differs from the texts of other seeds from its first character on
    inline String BlockText(size_t length, char seed) {
        String text(length, seed);
        for (size_t i = 1; i < length; i += 7) {
            text[i] = static_cast<char>('a' + (i + static_cast<size_t>(seed)) % 26);
        }
        return text;
    }

    /// BlockText(@p length, @p seed) that is not in the PoolString table of this process yet: its first characters are a FreshText
    inline String FreshBlockText(size_t length, char seed) {
        for (;;) {
            String text = BlockText(length, seed);
            const String tag = FreshText("block");
            text.replace(0, std::min(tag.size(), length), tag.data(), std::min(tag.size(), length));
            if (!PoolString::Find(StringView{text}).has_value()) {
                return text;
            }
        }
    }

    /// Runs `body(threadIndex)` on `count` threads that start together, waits up to `limit` and **always joins** every
    /// thread, so a test that fails on the result leaves no joinable `std::thread` behind. False when a thread had not
    /// finished by the deadline (the join then waits for it: a deadlock ends in the ctest timeout, not in a terminate).
    template <typename Body>
    [[nodiscard]] bool RunThreadsTogether(int count, std::chrono::milliseconds limit, Body body) {
        std::atomic<int> ready{0};
        std::atomic<int> finished{0};
        Vector<std::thread> threads;
        for (int t = 0; t < count; ++t) {
            threads.emplace_back([&, t] {
                ready.fetch_add(1);
                while (ready.load() < count) {
                    std::this_thread::yield();
                }
                body(t);
                finished.fetch_add(1);
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + limit;
        while (finished.load() < count && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        const bool inTime = finished.load() == count;
        for (std::thread& thread : threads) {
            thread.join();
        }
        return inTime;
    }

}  // namespace BECore::Tests
