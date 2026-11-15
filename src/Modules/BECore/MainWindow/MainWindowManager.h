#pragma once

#include <BECore/MainWindow/IMainWindow.h>
#include <BECore/MainWindow/MainWindowAccessor.h>
#include <BECore/RefCounted/IntrusivePtr.h>
#include <utility>

namespace BECore {

    class MainWindowManager {
    public:
        MainWindowManager() = default;
        ~MainWindowManager() = default;

        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

        void SetMainWindow(IntrusivePtr<IMainWindow> window) {
            _window = std::move(window);
        }

    private:
        IntrusivePtr<IMainWindow> _window;
    };

}  // namespace BECore
