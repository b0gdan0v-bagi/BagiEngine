#include <Application/Application.h>
#include <BECore/GameManager/CoreManager.h>

int RunApplication(int argc, const char* const* argv) {
    // Content roots, lowest priority first: the engine's config/ and assets/, then the game's on top of them.
    // be_add_game_executable() defines both; without them FileSystem searches upwards from the working directory.
#ifdef BAGI_ENGINE_ROOT
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_ENGINE_ROOT);
#endif
#ifdef BAGI_GAME_ROOT
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_GAME_ROOT);
#endif

    BECore::Application::GetInstance().SetArguments(argc, argv);

    if (!BECore::Application::GetInstance().Initialize()) {
        return -1;
    }

    BECore::Application::GetInstance().Run();

    return 0;
}

#ifdef PLATFORM_WINDOWS
#include <cstdlib>
#include <windows.h>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    // A WIN32 executable has no argc / argv of its own; the C runtime parses the command line into these
    return RunApplication(__argc, __argv);
}
#else
int main(int argc, char* argv[]) {
    return RunApplication(argc, argv);
}
#endif