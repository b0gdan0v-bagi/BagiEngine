#pragma once

#include <Core/MainWindow/IMainWindow.h>
#include <Core/ImGui/IimGuiManager.h>
#include <Core/Utils/IntrusivePtr.h>
#include <Core/Utils/Singleton.h>

namespace Core {

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize();
        void Run();
        void Cleanup();

        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

    private:
        IntrusivePtr<IMainWindow> _window;
        IntrusivePtr<IimGuiManager> _imguiManager;
        bool _isRunning = false;
    };
}  // namespace Core
