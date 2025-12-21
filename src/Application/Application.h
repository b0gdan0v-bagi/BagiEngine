#pragma once

#include <SDL3/SDL.h>
#include "Core/MainWindow/IMainWindow.h"
#include "Core/Utils/IntrusivePtr.h"

class Application {
public:
    Application() = default;
    ~Application();

    bool Initialize();
    void Run();
    void Cleanup();

private:
    IntrusivePtr<IMainWindow> _window;
    bool _isRunning = false;
};

