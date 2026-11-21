#pragma once

#include <BECore/Types/Types.h>
#include <cstddef>
#include <type_traits>
#include <utility>

namespace BECore::Detail {

    /**
     * A double-ended queue of move-only elements over one array whose size is a power of two. Push and pop at either end
     * allocate nothing once the array has grown to the peak of the queue; the array only grows (by doubling) and never
     * shrinks, so a steady queue never touches the heap, where a deque frees and allocates a subarray every time its
     * ends cross a block boundary. Not thread-safe: the callers hold their mutex.
     *
     * Every slot holds a live object (`T{}` when unused), so a slot needs no placement new; a popped slot is left
     * moved-from, which for MoveOnlyFunction is empty, so the capture of a popped job dies with the job, not when
     * the slot is overwritten. A destructor of an element must not touch the ring that holds it (Clear destroys
     * the elements one by one).
     */
    template <typename T>
    class MoveRing {
        static_assert(std::is_default_constructible_v<T> && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>,
                      "the ring default constructs its slots and moves elements without failing");

    public:
        void PushBack(T value) {
            Reserve();
            _slots[SlotOf(_size)] = std::move(value);
            ++_size;
        }

        [[nodiscard]] T PopFront() {
            T value = std::move(_slots[_head]);
            _head = (_head + 1) & Mask();
            --_size;
            return value;
        }

        [[nodiscard]] T PopBack() {
            --_size;
            return std::move(_slots[SlotOf(_size)]);
        }

        [[nodiscard]] T& Front() {
            return _slots[_head];
        }

        [[nodiscard]] const T& Front() const {
            return _slots[_head];
        }

        [[nodiscard]] size_t Size() const {
            return _size;
        }

        [[nodiscard]] bool Empty() const {
            return _size == 0;
        }

        /// Destroys each live element once, oldest first, and keeps the array
        void Clear() {
            for (size_t i = 0; i < _size; ++i) {
                _slots[SlotOf(i)] = T{};
            }
            _head = 0;
            _size = 0;
        }

    private:
        static constexpr size_t kInitialCapacity = 8;

        [[nodiscard]] size_t Mask() const {
            return _slots.size() - 1;
        }

        [[nodiscard]] size_t SlotOf(size_t index) const {
            return (_head + index) & Mask();
        }

        /// Makes room for one more element, moving the live ones to the front of a larger array in order
        void Reserve() {
            if (_size < _slots.size()) {
                return;
            }
            Vector<T> bigger;
            bigger.resize(_slots.empty() ? kInitialCapacity : _slots.size() * 2);
            for (size_t i = 0; i < _size; ++i) {
                bigger[i] = std::move(_slots[SlotOf(i)]);
            }
            _slots.swap(bigger);
            _head = 0;
        }

        Vector<T> _slots;
        size_t _head = 0;
        size_t _size = 0;
    };

}  // namespace BECore::Detail
