#include <Application/Application.h>

#ifdef PLATFORM_WINDOWS
#include <windows.h>

int RunApplication() {
    if (!BECore::Application::GetInstance().Initialize()) {
        return -1;
    }

    BECore::Application::GetInstance().Run();

    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return RunApplication();
}
#else
int main(int argc, char* argv[]) {
    return RunApplication();
}
#endif