#include <Application/Application.h>
#include <BECore/Application/ApplicationMainAccess.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>

int RunApplication() {
    if (!BECore::Application::GetInstance().Initialize(BECore::PassKey<BECore::ApplicationMainAccess>{})) {
        return -1;
    }

    BECore::Application::GetInstance().Run(BECore::PassKey<BECore::ApplicationMainAccess>{});

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return RunApplication();
}
#else
int main(int argc, char* argv[]) {
    if (!BECore::Application::GetInstance().Initialize(BECore::PassKey<BECore::ApplicationMainAccess>{})) {
        return -1;
    }

    BECore::Application::GetInstance().Run(BECore::PassKey<BECore::ApplicationMainAccess>{});

    return 0;
}
#endif