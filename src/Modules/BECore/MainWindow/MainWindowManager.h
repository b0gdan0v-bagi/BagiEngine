#pragma once

#include <BECore/MainWindow/MainWindowAccessor.h>
#include <BECore/MainWindow/IMainWindow.h>

namespace BECore {

    class MainWindowManager {
    public:
        MainWindowManager() = default;
        ~MainWindowManager() = default;

        // Геттер для главного окна
        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

        // Установка главного окна
        void SetMainWindow(IntrusivePtr<IMainWindow> window) {
            _window = std::move(window);
        }

    private:
        IntrusivePtr<IMainWindow> _window;
    };

}  // namespace BECore

