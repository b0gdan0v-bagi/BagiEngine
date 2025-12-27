#pragma once

#include <Core/MainWindow/IMainWindow.h>
#include <Core/ImGui/IimGuiManager.h>
#include <Core/Utils/IntrusivePtr.h>
#include <Core/Utils/Singleton.h>
#include <Widgets/WidgetManager.h>

namespace Core {

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize();
        void Run();
        void Cleanup();
        void StopApplication();

        const IntrusivePtr<IMainWindow>& GetMainWindow() const {
            return _window;
        }

    private:
        IntrusivePtr<IMainWindow> _window;
        IntrusivePtr<IimGuiManager> _imguiManager;
        WidgetManager _widgetManager;
        bool _isRunning = false;
    };
}  // namespace Core
