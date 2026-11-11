#include "SDLMainWindow.h"

#include "BECore/Utils/ScopeGuard.h"
#include "SDLWebCanvas.h"
#include "SDLWindowConfig.h"

#include <BECore/Reflection/OpenDeserializer.h>
#include <CoreSDL/SDLUtils.h>
#include <Events/ApplicationEvents.h>
#include <Generated/SDLWindowConfig.gen.hpp>

namespace BECore {

    namespace {

        constexpr const char* kHeadlessVideoDriver = "dummy";

    }  // namespace

    SDLMainWindow::SDLMainWindow() = default;
    SDLMainWindow::~SDLMainWindow() = default;

    bool SDLMainWindow::Initialize(eastl::string_view configPath) {
        BE_PROFILE_SCOPE("SDLMainWindow::Initialize");

        // The video driver is read by SDL_Init, so the hint has to come first
        if (_headless) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, kHeadlessVideoDriver);
        }

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }

        bool initialized = false;
        ScopeGuard guard([&]() {
            if (!initialized) {
                SDL_Quit();
                SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
            }
        });

        auto opened = OpenDeserializer(configPath);
        if (!opened) {
            LOG_ERROR(Format("[SDLMainWindow] {}", opened.error()).c_str());
            return false;
        }
        IDeserializer& deserializer = **opened;

        SDLWindowConfig windowConfig;
        if (!deserializer.BeginObject("window")) {
            return false;
        }
        windowConfig.Deserialize(deserializer);
        deserializer.EndObject();
        LogDeserializeErrors(configPath, deserializer);

        SDL_WindowFlags flags = 0;
        if (!windowConfig._windowFlags.empty()) {
            flags = SDLUtils::ParseWindowFlags(eastl::string_view(windowConfig._windowFlags.data(), windowConfig._windowFlags.size()));
        }
        if (_headless) {
            flags &= ~SDL_WINDOW_VULKAN;  // the dummy driver has no Vulkan surface; the software renderer draws instead
        }
#ifdef __EMSCRIPTEN__
        // The browser renderer is SDL's GLES2 one, which recreates a window that lacks SDL_WINDOW_OPENGL. The recreation unregisters
        // every page handler of resize, fullscreenchange and orientationchange (Emscripten removes them by type, not by owner),
        // InstallWebCanvasResize's too, and the canvas would stop following the page in fullscreen and on rotation
        flags = (flags & ~SDL_WINDOW_VULKAN) | SDL_WINDOW_OPENGL;
#endif

        _window = SDL_CreateWindow(windowConfig._title.c_str(), windowConfig._width, windowConfig._height, flags);
        if (!_window) {
            return false;
        }

        _width = windowConfig._width;
        _height = windowConfig._height;

#ifdef __EMSCRIPTEN__
        // The canvas fills the browser window: the size of the config is only the start size
        InstallWebCanvasResize(_window);
#endif

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
        SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);  // the next window picks its driver again
        _width = 0;
        _height = 0;
    }

    bool SDLMainWindow::IsValid() const {
        return _window != nullptr;
    }

    int SDLMainWindow::GetWidth() const {
        // The live size in window units (the window is resizable and starts maximized), the config's while SDL cannot tell
        int width = 0;
        if (_window != nullptr && SDL_GetWindowSize(_window, &width, nullptr) && width > 0) {
            return width;
        }
        return _width;
    }

    int SDLMainWindow::GetHeight() const {
        int height = 0;
        if (_window != nullptr && SDL_GetWindowSize(_window, nullptr, &height) && height > 0) {
            return height;
        }
        return _height;
    }

    float SDLMainWindow::GetPixelDensity() const {
        // 0 is SDL's answer for "failed" (and for no window)
        const float density = _window != nullptr ? SDL_GetWindowPixelDensity(_window) : 0.0f;
        return density > 0.0f ? density : IMainWindow::GetPixelDensity();
    }

    float SDLMainWindow::GetDisplayScale() const {
        const float scale = _window != nullptr ? SDL_GetWindowDisplayScale(_window) : 0.0f;
        return scale > 0.0f ? scale : IMainWindow::GetDisplayScale();
    }

    Rect SDLMainWindow::GetSafeArea() const {
#ifdef __EMSCRIPTEN__
        // SDL's driver answers the whole window; the insets of the page (a notch, the system bars) are the web's own
        if (_window != nullptr) {
            const WebSafeArea page = GetWebSafeArea(GetWidth(), GetHeight());
            return Rect(static_cast<float>(page.x), static_cast<float>(page.y), static_cast<float>(page.width), static_cast<float>(page.height));
        }
#endif
        SDL_Rect area{};
        if (_window == nullptr || !SDL_GetWindowSafeArea(_window, &area) || area.w <= 0 || area.h <= 0) {
            return IMainWindow::GetSafeArea();
        }
        return Rect(static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.w), static_cast<float>(area.h));
    }

}  // namespace BECore
