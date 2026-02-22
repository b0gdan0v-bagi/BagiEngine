#include "SDLMainWindow.h"

#include "BECore/Utils/ScopeGuard.h"
#include "SDLWindowConfig.h"

#include <BECore/Reflection/XmlDeserializer.h>
#include <CoreSDL/SDLUtils.h>
#include <Events/ApplicationEvents.h>
#include <Generated/SDLWindowConfig.gen.hpp>

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

        XmlConfig config = XmlConfig::Create();
        if (!config.LoadFromVirtualPath(configPath)) {
            return false;
        }

        auto windowNode = config.GetRoot().GetChild("window");
        if (!windowNode) {
            return false;
        }

        SDLWindowConfig windowConfig;
        XmlDeserializer deserializer;
        deserializer.LoadFromXmlNode(windowNode);
        windowConfig.Deserialize(deserializer);

        SDL_WindowFlags flags = 0;
        if (!windowConfig._windowFlags.empty()) {
            flags = SDLUtils::ParseWindowFlags(eastl::string_view(windowConfig._windowFlags.data(), windowConfig._windowFlags.size()));
        }

        _window = SDL_CreateWindow(windowConfig._title.c_str(), windowConfig._width, windowConfig._height, flags);
        if (!_window) {
            return false;
        }

        _width = windowConfig._width;
        _height = windowConfig._height;

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
