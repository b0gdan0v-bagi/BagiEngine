#pragma once

#include <utility>

namespace BECore {

    /// Runs the callback when the object is destroyed; the callable is stored by value, with no type erasure and no allocation.
    template <typename TCallable>
    class ScopeGuard {
    public:
        explicit ScopeGuard(TCallable onExit) : _onExit(std::move(onExit)) {}

        ~ScopeGuard() {
            _onExit();
        }

        // A copy would run the callback twice.
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        TCallable _onExit;
    };

    template <typename TCallable>
    ScopeGuard(TCallable) -> ScopeGuard<TCallable>;

}  // namespace BECore
