#include <Application/Application.h>
#include <BECore/FileSystem/FileSystem.h>
#include <BECore/GameManager/CoreManager.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#ifndef BAGI_WEB_ROOT
// Defined by the generated source of the target (be_link_content_roots): the only source that names this checkout's paths
namespace BagiContentRoots {
    extern const char* const kEngine;
    extern const char* const kGame;
}  // namespace BagiContentRoots
#endif

int RunApplication(int argc, const char* const* argv) {
    // Content roots, lowest priority first: the engine's config/ and assets/, then the game's on top of them.
    // The web build's roots are virtual paths (-D defines of cmake/BagiWeb.cmake) and BAGI_WEB_ROOT, its config overlay,
    // comes last, so it has the highest priority.
#ifdef BAGI_WEB_ROOT
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_ENGINE_ROOT);
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_GAME_ROOT);
    BECore::CoreManager::GetFileSystem().AddContentRoot(BAGI_WEB_ROOT);
#else
    BECore::CoreManager::GetFileSystem().AddContentRoot(BagiContentRoots::kEngine);
    BECore::CoreManager::GetFileSystem().AddContentRoot(BagiContentRoots::kGame);
#endif

    BECore::Application::GetInstance().SetArguments(argc, argv);

    if (!BECore::Application::GetInstance().Initialize()) {
        return -1;
    }

#ifdef __EMSCRIPTEN__
    // The browser owns the loop: one RunFrame() per animation frame. Never returns (simulate_infinite_loop), so the stack of
    // main stays alive for the callback; at the end of the application the loop is cancelled and the shutdown runs
    emscripten_set_main_loop_arg(
        [](void*) {
            if (!BECore::Application::GetInstance().RunFrame()) {
                emscripten_cancel_main_loop();
                BECore::Application::GetInstance().Finish();
            }
        },
        nullptr, 0, true);  // fps 0: requestAnimationFrame
#else
    BECore::Application::GetInstance().Run();
#endif

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