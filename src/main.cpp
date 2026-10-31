#include <Application/Application.h>
#include <BECore/GameManager/CoreManager.h>

int RunApplication() {
    // Content roots, lowest priority first: the engine's config/ and assets/, then the game's on top of them.
    // be_add_game_executable() defines both; without them FileSystem searches upwards from the working directory.
#ifdef BAGI_ENGINE_ROOT
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_ENGINE_ROOT);
#endif
#ifdef BAGI_GAME_ROOT
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_GAME_ROOT);
#endif

    if (!BECore::Application::GetInstance().Initialize()) {
        return -1;
    }

    BECore::Application::GetInstance().Run();

    return 0;
}

#ifdef PLATFORM_WINDOWS
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return RunApplication();
}
#else
int main(int argc, char* argv[]) {
    return RunApplication();
}
#endif