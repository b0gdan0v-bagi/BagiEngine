#pragma once

namespace Core {
    class ScopeGuard {
    public:
        // Принимаем любой вызываемый объект
        explicit ScopeGuard(std::function<void()> onExit) : _onExit(std::move(onExit)) {}

        // Деструктор сработает при выходе из {}
        ~ScopeGuard() {
            if (_onExit) {
                _onExit();
            }
        }

        // Запрещаем копирование, чтобы избежать двойного вызова
        ScopeGuard(const ScopeGuard&) = delete;
        ScopeGuard& operator=(const ScopeGuard&) = delete;

    private:
        std::function<void()> _onExit;
    };
}  // namespace Core

