#pragma once

#include <Core/MainWindow/MainWindowAccessor.h>
#include <Core/MainWindow/IMainWindow.h>
#include <Core/RefCounted/IntrusivePtr.h>
#include <Core/Utils/PassKey.h>
#include <Core/Utils/Singleton.h>
#include <Core/GameManager/CoreManager.h>

namespace Core {

    // Forward declaration для класса-ключа доступа
    class ApplicationMainAccess;

    class Application : public Singleton<Application> {
    public:
        Application() = default;
        ~Application() override = default;

        bool Initialize(PassKey<ApplicationMainAccess>);
        void Run(PassKey<ApplicationMainAccess>) const;

    private:

        void StopApplication();
        static void Cleanup();

        bool _isRunning = false;
    };
}  // namespace Core
