#pragma once

#include <functional>
#include <utility>

namespace BECore {
    /// Runs the callback when the object is destroyed.
    class ScopeGuard {
    public:
        explicit ScopeGuard(std::function<void()> onExit) : _onExit(std::move(onExit)) {}

        ~ScopeGuard() {
            if (_onExit) {
                _onExit();
            }
        }

        // A copy would run the callback twice.
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        std::function<void()> _onExit;
    };
}  // namespace BECore
