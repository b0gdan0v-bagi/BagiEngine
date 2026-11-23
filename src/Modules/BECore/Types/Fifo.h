#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <BECore/Memory/EngineAllocator.h>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace BECore {

    /**
     * @brief A queue on a growable ring buffer that keeps its capacity: pushes at the back, pops at the front, and after the first few
     * growths a steady producer and consumer allocate nothing. `eastl::deque` frees and allocates a sub-array every few pushes, which the
     * message queues of the transport and the control service (RemoteControl/CLAUDE.md, BECore/Control/CLAUDE.md) pay for on every frame.
     *
     * - **Storage:** one `EngineAllocator` block whose capacity is a power of two, so an index wraps with a mask. It doubles when full and
     *   never shrinks; `clear()` destroys the elements and keeps the block.
     * - **Elements move, never copy:** the type may be move-only (`UniquePtr`, `MoveOnlyFunction`). Growth moves them in order.
     * - **Invalidation:** a reference to an element (`front()`, `operator[]`) is valid until the next `push_back` / `emplace_back` /
     *   `reserve` that grows, and until that element is popped. An argument that refers to an element of the queue itself is safe to push.
     * - **Not a deque:** no iterators, no insert or erase in the middle, no `push_front`. Not thread-safe (the owners hold their own lock).
     */
    template <typename T>
    class Fifo {
    public:
        static_assert(alignof(T) <= __STDCPP_DEFAULT_NEW_ALIGNMENT__, "Fifo storage comes from the plain allocation");

        Fifo() = default;
        Fifo(const Fifo&) = delete;
        Fifo& operator=(const Fifo&) = delete;

        Fifo(Fifo&& other) noexcept {
            Steal(other);
        }

        Fifo& operator=(Fifo&& other) noexcept {
            if (this != &other) {
                Release();
                Steal(other);
            }
            return *this;
        }

        ~Fifo() {
            Release();
        }

        void push_back(T&& value) {
            emplace_back(std::move(value));
        }

        void push_back(const T& value) {
            emplace_back(value);
        }

        template <typename... Args>
        T& emplace_back(Args&&... args) {
            if (_size == _capacity) {
                // The element is built in the new block before the old ones move, so an argument that refers to an element of this queue
                // is still intact when it is read
                return GrowAndEmplace(std::forward<Args>(args)...);
            }
            T* slot = ::new (static_cast<void*>(_data + Wrap(_head + _size))) T(std::forward<Args>(args)...);
            ++_size;
            return *slot;
        }

        [[nodiscard]] T& front() {
            ASSERT(_size > 0, "front() of an empty Fifo");
            return _data[_head];
        }

        [[nodiscard]] const T& front() const {
            ASSERT(_size > 0, "front() of an empty Fifo");
            return _data[_head];
        }

        /// Destroys the front element; the queue must not be empty
        void pop_front() {
            ASSERT(_size > 0, "pop_front() of an empty Fifo");
            _data[_head].~T();
            _head = Wrap(_head + 1);
            --_size;
        }

        /// The i-th element from the front
        [[nodiscard]] T& operator[](size_t index) {
            ASSERT(index < _size, "Fifo index out of range");
            return _data[Wrap(_head + index)];
        }

        [[nodiscard]] const T& operator[](size_t index) const {
            ASSERT(index < _size, "Fifo index out of range");
            return _data[Wrap(_head + index)];
        }

        [[nodiscard]] size_t size() const {
            return _size;
        }

        [[nodiscard]] bool empty() const {
            return _size == 0;
        }

        [[nodiscard]] size_t capacity() const {
            return _capacity;
        }

        /// Makes room for at least `count` elements (rounded up to a power of two); never shrinks
        void reserve(size_t count) {
            if (count > _capacity) {
                size_t capacity = _capacity == 0 ? kMinCapacity : _capacity;
                while (capacity < count) {
                    capacity *= 2;
                }
                Reallocate(capacity);
            }
        }

        /// Destroys every element and keeps the storage
        void clear() {
            DestroyAll();
            _head = 0;
        }

        void swap(Fifo& other) noexcept {
            std::swap(_data, other._data);
            std::swap(_head, other._head);
            std::swap(_size, other._size);
            std::swap(_capacity, other._capacity);
        }

    private:
        static constexpr size_t kMinCapacity = 8;

        [[nodiscard]] size_t Wrap(size_t index) const {
            return index & (_capacity - 1);
        }

        void Steal(Fifo& other) {
            _data = std::exchange(other._data, nullptr);
            _head = std::exchange(other._head, 0);
            _size = std::exchange(other._size, 0);
            _capacity = std::exchange(other._capacity, 0);
        }

        void DestroyAll() {
            for (size_t i = 0; i < _size; ++i) {
                _data[Wrap(_head + i)].~T();
            }
            _size = 0;
        }

        void Release() {
            DestroyAll();
            if (_data != nullptr) {
                EngineAllocator().deallocate(_data, _capacity * sizeof(T));
            }
            _data = nullptr;
            _head = 0;
            _capacity = 0;
        }

        [[nodiscard]] static T* Allocate(size_t capacity) {
            return static_cast<T*>(EngineAllocator().allocate(capacity * sizeof(T)));
        }

        // Moves the live elements to the front of a block of `capacity` slots
        void Reallocate(size_t capacity) {
            T* data = Allocate(capacity);
            MoveInto(data);
            Replace(data, capacity);
        }

        void MoveInto(T* target) {
            for (size_t i = 0; i < _size; ++i) {
                T& source = _data[Wrap(_head + i)];
                ::new (static_cast<void*>(target + i)) T(std::move(source));
                source.~T();
            }
        }

        void Replace(T* data, size_t capacity) {
            if (_data != nullptr) {
                EngineAllocator().deallocate(_data, _capacity * sizeof(T));
            }
            _data = data;
            _head = 0;
            _capacity = capacity;
        }

        template <typename... Args>
        T& GrowAndEmplace(Args&&... args) {
            const size_t capacity = _capacity == 0 ? kMinCapacity : _capacity * 2;
            T* data = Allocate(capacity);
            T* slot = ::new (static_cast<void*>(data + _size)) T(std::forward<Args>(args)...);
            MoveInto(data);
            Replace(data, capacity);
            ++_size;
            return *slot;
        }

        T* _data = nullptr;
        size_t _head = 0;
        size_t _size = 0;
        size_t _capacity = 0;
    };

}  // namespace BECore
