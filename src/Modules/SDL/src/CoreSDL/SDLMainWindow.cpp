#include "SDLMainWindow.h"

#include "BECore/Utils/ScopeGuard.h"

#include <CoreSDL/SDLUtils.h>
#include <Events/ApplicationEvents.h>

namespace BECore {

    SDLMainWindow::SDLMainWindow() = default;
    SDLMainWindow::~SDLMainWindow() = default;

    bool SDLMainWindow::Initialize(eastl::string_view configPath) {

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }

        bool initialized = false;
        ScopeGuard guard([&]() {
            if (!initialized) {
                SDL_Quit();
            }
        });

        std::string title;
        int width;
        int height;
        SDL_WindowFlags flags = 0;

        XmlConfig config = XmlConfig::Create();

        if (config.LoadFromVirtualPath(configPath)) {
            auto windowNode = config.GetRoot().GetChild("window");
            if (!windowNode) {
                return false;
            }

            title = windowNode.ParseAttribute<std::string>("title").value_or("My SDL3 Window");
            width = windowNode.ParseAttribute<int>("width").value_or(800);
            height = windowNode.ParseAttribute<int>("height").value_or(600);

            const auto flagsStringOpt = windowNode.ParseAttribute<eastl::string_view>("windowFlags");
            if (flagsStringOpt && !flagsStringOpt->empty()) {
                flags = SDLUtils::ParseWindowFlags(*flagsStringOpt);
            }
        } else {
            return false;
        }

        _window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (!_window) {
            return false;
        }

        _width = width;
        _height = height;

        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLMainWindow::Destroy>(this);

        initialized = true;
        return true;
    }

    void SDLMainWindow::Destroy() {
        if (_window != nullptr) {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }
        SDL_Quit();
        _width = 0;
        _height = 0;
    }

    bool SDLMainWindow::IsValid() const {
        return _window != nullptr;
    }

    int SDLMainWindow::GetWidth() const {
        return _width;
    }

    int SDLMainWindow::GetHeight() const {
        return _height;
    }

}  // namespace BECore
