#pragma once

#include <BECore/Assert/AssertMacros.h>
#include <atomic>
#include <cstdint>

namespace BECore {

    /**
     * Intrusive counter for one thread (or for objects that are handed over with their own synchronisation).
     *
     * The count starts at 0: a fresh object has no owner. An IntrusivePtr(this) made and dropped inside the constructor, or before
     * any other owner exists, brings the count to 1 and back to 0 and deletes the object; make the first owner with New<T>.
     */
    class RefCountedNonAtomic {
    public:
        RefCountedNonAtomic() = default;
        virtual ~RefCountedNonAtomic() {
            ASSERT(_refCount == 0, "RefCounted object destroyed while it is still held (stack object or direct delete)");
        }

        void AddRef() const noexcept {
            ++_refCount;
        }

        void ReleaseRef() const noexcept {
            ASSERT(_refCount > 0);
            --_refCount;
            if (_refCount == 0) {
                delete this;
            }
        }

        /// A snapshot of the count, not a promise: it can change as soon as it is read.
        int32_t GetRefCount() const noexcept {
            return _refCount;
        }

    protected:
        RefCountedNonAtomic(const RefCountedNonAtomic&) = delete;
        RefCountedNonAtomic& operator=(const RefCountedNonAtomic&) = delete;
        RefCountedNonAtomic(RefCountedNonAtomic&&) = delete;
        RefCountedNonAtomic& operator=(RefCountedNonAtomic&&) = delete;

    private:
        mutable int32_t _refCount = 0;
    };

    /// The counter of an object several threads hold; the same rules as RefCountedNonAtomic, including the count that starts at 0.
    class RefCountedAtomic {
    public:
        RefCountedAtomic() = default;
        virtual ~RefCountedAtomic() {
            ASSERT(_refCount.load(std::memory_order_relaxed) == 0, "RefCounted object destroyed while it is still held (stack object or direct delete)");
        }

        void AddRef() const noexcept {
            _refCount.fetch_add(1, std::memory_order_relaxed);
        }

        void ReleaseRef() const noexcept {
            const int32_t oldCount = _refCount.fetch_sub(1, std::memory_order_acq_rel);
            ASSERT(oldCount > 0);
            if (oldCount == 1) {
                delete this;
            }
        }

        /// A snapshot of the count, not a promise: other threads change it while it is read, so it orders nothing.
        int32_t GetRefCount() const noexcept {
            return _refCount.load(std::memory_order_relaxed);
        }

    protected:
        RefCountedAtomic(const RefCountedAtomic&) = delete;
        RefCountedAtomic& operator=(const RefCountedAtomic&) = delete;
        RefCountedAtomic(RefCountedAtomic&&) = delete;
        RefCountedAtomic& operator=(RefCountedAtomic&&) = delete;

    private:
        mutable std::atomic<int32_t> _refCount = 0;
    };

    using RefCounted = RefCountedNonAtomic;

}  // namespace BECore
