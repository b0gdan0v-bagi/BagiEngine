#pragma once

#include <Core/MainWindow/MainWindowAccessor.h>
#include <Core/MainWindow/IMainWindow.h>
#include <Core/RefCounted/IntrusivePtr.h>
#include <Core/Utils/PassKey.h>

namespace Core {

    class MainWindowManager {
    public:
        MainWindowManager() = default;
        ~MainWindowManager() = default;

        // Геттер для главного окна
        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

        // Установка главного окна
        void SetMainWindow(IntrusivePtr<IMainWindow> window, PassKey<MainWindowAccessor>) {
            _window = std::move(window);
        }

    private:
        IntrusivePtr<IMainWindow> _window;
    };

}  // namespace Core

