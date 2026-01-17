#pragma once

namespace BECore {

class RefCountedNonAtomic {
   public:
    RefCountedNonAtomic() = default;
    virtual ~RefCountedNonAtomic() = default;

    void AddRef() const {
        ++_refCount;
    }

    bool ReleaseRef() const {
        --_refCount;
        if (_refCount == 0) {
            delete this;
            return true;
        }
        return false;
    }

    int32_t GetRefCount() const {
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

class RefCountedAtomic {
   public:
    RefCountedAtomic() = default;
    virtual ~RefCountedAtomic() = default;

    void AddRef() const {
        _refCount.fetch_add(1, std::memory_order_relaxed);
    }

    bool ReleaseRef() const {
        int32_t oldCount = _refCount.fetch_sub(1, std::memory_order_acq_rel);
        if (oldCount == 1) {
            delete this;
            return true;
        }
        return false;
    }

    int32_t GetRefCount() const {
        return _refCount.load(std::memory_order_acquire);
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

