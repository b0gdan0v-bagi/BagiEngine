#pragma once

#include <Events/EventsProviderManager.h>
#include <BECore/MainWindow/MainWindowManager.h>
#include <BECore/Widgets/WidgetManager.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/Assert/AssertHandlers.h>
#include <BECore/Logger/LoggerManager.h>
#include <BECore/Tests/TestManager.h>

namespace BECore {

    // Forward declarations
    class Application;
    class TaskManager;

    class CoreManager : public Singleton<CoreManager> {
    public:
        CoreManager() = default;
        ~CoreManager() override = default;

        static WidgetManager& GetWidgetManager() {
            return GetInstance()._widgetManager;
        }

        static EventsProviderManager& GetEventsProviderManager() {
            return GetInstance()._eventsProviderManager;
        }

        static MainWindowManager& GetMainWindowManager() {
            return GetInstance()._mainWindowManager;
        }

        static FileSystem& GetFileSystem() {
            return GetInstance()._fileSystem;
        }

        static AssertHandlerManager& GetAssertHandlerManager() {
            return GetInstance()._assertHandlerManager;
        }

        static TestManager& GetTestManager() {
            return GetInstance()._testManager;
        }

        static LoggerManager& GetLoggerManager() {
            return GetInstance()._loggerManager;
        }

        static TaskManager& GetTaskManager();

        static const IntrusivePtr<IMainWindow>& GetMainWindow();

        // Pre-initialization function (FileSystem, AssertHandlers, Tests)
        void OnApplicationPreInit(PassKey<Application>);

        // Application initialization function
        void OnApplicationInit(PassKey<Application>);

        // Функция игрового цикла
        void OnGameCycle(PassKey<Application>) const;

        // Функция деинициализации приложения
        void OnApplicationDeinit(PassKey<Application>);

    private:
        FileSystem _fileSystem;
        LoggerManager _loggerManager;
        AssertHandlerManager _assertHandlerManager;
        TestManager _testManager;
        WidgetManager _widgetManager;
        EventsProviderManager _eventsProviderManager;
        MainWindowManager _mainWindowManager;
    };

}  // namespace BECore

