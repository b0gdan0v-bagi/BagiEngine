#include "SDLMainWindow.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/MainWindow/WindowTitle.h>
#include <BECore/Reflection/OpenDeserializer.h>
#include <BECore/Utils/ScopeGuard.h>
#include <CoreSDL/SDLEvents.h>
#include <CoreSDL/SDLUtils.h>
#include <CoreSDL/SDLWebCanvas.h>
#include <CoreSDL/SDLWindowConfig.h>
#include <Events/ApplicationEvents.h>
#include <Generated/SDLWindowConfig.gen.hpp>

namespace BECore {

    namespace {

        constexpr const char* kHeadlessVideoDriver = "dummy";

        bool IsSameRect(const Rect& a, const Rect& b) {
            return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
        }

    }  // namespace

    SDLMainWindow::SDLMainWindow() = default;
    SDLMainWindow::~SDLMainWindow() = default;

    bool SDLMainWindow::Initialize(StringView configPath) {
        BE_PROFILE_SCOPE("SDLMainWindow::Initialize");

        // The video driver is read by SDL_Init, so the hint has to come first
        if (_headless) {
            SDL_SetHint(SDL_HINT_VIDEO_DRIVER, kHeadlessVideoDriver);
        }

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            LOG_ERROR("[SDLMainWindow] cannot start the video system: {}", SDL_GetError());
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
            LOG_ERROR("[SDLMainWindow] {}", opened.error());
            return false;
        }
        IDeserializer& deserializer = **opened;
        deserializer.SetDiagnostics(ConfigManager::GetDiagnostics());

        SDLWindowConfig windowConfig;
        if (!deserializer.BeginObject("window")) {
            return false;
        }
        windowConfig.Deserialize(deserializer);
        deserializer.EndObject();
        deserializer.FinishLoad();
        LogDeserializeErrors(configPath, deserializer);

        SDL_WindowFlags flags = 0;
        if (!windowConfig._windowFlags.empty()) {
            flags = SDLUtils::ParseWindowFlags(StringView(windowConfig._windowFlags));
        }
        // A headless window loses SDL_WINDOW_VULKAN: the dummy driver has no Vulkan surface, the software renderer draws instead
        flags = SDLUtils::ApplyStartOptions(flags, {.headless = _headless, .hidden = _hidden && !_headless, .backend = _backend});
#ifdef __EMSCRIPTEN__
        // The browser renderer is SDL's GLES2 one, which recreates a window that lacks SDL_WINDOW_OPENGL. The recreation unregisters
        // every page handler of resize, fullscreenchange and orientationchange (Emscripten removes them by type, not by owner),
        // InstallWebCanvasResize's too, and the canvas would stop following the page in fullscreen and on rotation
        flags = (flags & ~SDL_WINDOW_VULKAN) | SDL_WINDOW_OPENGL;
#endif

        // The first title; Application sets the composed one once the game is known
        const String firstTitle = ComposeWindowTitle(windowConfig._title, {}, false);
        // The dummy driver has no density, so its window is as big as the output in pixels
        _simulation = WindowSimulation{.width = windowConfig._width, .height = windowConfig._height};
        if (_headless && (_startSize || _startDensity)) {
            if (_startSize) {
                _simulation.width = _startSize->width;
                _simulation.height = _startSize->height;
            }
            _simulation.density = _startDensity.value_or(1.0f);
            if (const auto valid = ValidateWindowSimulation(_simulation); !valid) {
                LOG_ERROR("[SDLMainWindow] the start size of the headless window: {}", valid.error());
                _simulation = WindowSimulation{};
                return false;
            }
        }
        const int createWidth = _headless ? SimulatedOutputPixels(_simulation.width, _simulation.density) : windowConfig._width;
        const int createHeight = _headless ? SimulatedOutputPixels(_simulation.height, _simulation.density) : windowConfig._height;
        _window = SDL_CreateWindow(firstTitle.c_str(), createWidth, createHeight, flags);
        if (!_window) {
            LOG_ERROR("[SDLMainWindow] cannot create the window: {}", SDL_GetError());  // before the guard quits SDL, which clears the error
            return false;
        }

        _width = _simulation.width;
        _height = _simulation.height;
        _configuredTitle = windowConfig._title;

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
        _simulation = WindowSimulation{};
        _configuredTitle.clear();
    }

    bool SDLMainWindow::IsValid() const {
        return _window != nullptr;
    }

    int SDLMainWindow::GetWidth() const {
        if (IsSimulated()) {
            return _simulation.width;
        }
        // The live size in window units (the window is resizable and starts maximized), the config's while SDL cannot tell
        int width = 0;
        if (_window != nullptr && SDL_GetWindowSize(_window, &width, nullptr) && width > 0) {
            return width;
        }
        return _width;
    }

    int SDLMainWindow::GetHeight() const {
        if (IsSimulated()) {
            return _simulation.height;
        }
        int height = 0;
        if (_window != nullptr && SDL_GetWindowSize(_window, nullptr, &height) && height > 0) {
            return height;
        }
        return _height;
    }

    float SDLMainWindow::GetPixelDensity() const {
        if (IsSimulated()) {
            return _simulation.density;
        }
        // 0 is SDL's answer for "failed" (and for no window)
        const float density = _window != nullptr ? SDL_GetWindowPixelDensity(_window) : 0.0f;
        return density > 0.0f ? density : IMainWindow::GetPixelDensity();
    }

    float SDLMainWindow::GetDisplayScale() const {
        const float scale = _window != nullptr ? SDL_GetWindowDisplayScale(_window) : 0.0f;
        return scale > 0.0f ? scale : IMainWindow::GetDisplayScale();
    }

    void SDLMainWindow::SetTitle(StringView title) {
        if (_window == nullptr) {
            return;
        }
        const String text(title.data(), title.size());  // SDL wants a terminated string
        SDL_SetWindowTitle(_window, text.c_str());
    }

    std::expected<void, String> SDLMainWindow::Simulate(const WindowSimulation& simulation) {
        if (!_headless) {
            return std::unexpected(String("this window is a real window, only a headless window can be simulated"));
        }
        if (_window == nullptr) {
            return std::unexpected(String("the window does not exist"));
        }

        const Rect previousSafeArea = GetSafeArea();
        const int pixelWidth = SimulatedOutputPixels(simulation.width, simulation.density);
        const int pixelHeight = SimulatedOutputPixels(simulation.height, simulation.density);
        if (!SDL_SetWindowSize(_window, pixelWidth, pixelHeight)) {
            return std::unexpected(Format("SDL cannot resize the window to {}x{} pixels: {}", pixelWidth, pixelHeight, SDL_GetError()));
        }
        // The dummy driver applies the size at once; the sync makes sure of it for a driver that does not, so the renderer asked next sees the new output
        SDL_SyncWindow(_window);

        _simulation = simulation;
        if (!IsSameRect(GetSafeArea(), previousSafeArea)) {
            SDLEvents::SafeAreaChangedEvent::Emit();
        }
        return {};
    }

    Rect SDLMainWindow::GetSafeArea() const {
#ifdef __EMSCRIPTEN__
        // SDL's driver answers the whole window; the insets of the page (a notch, the system bars) are the web's own
        if (_window != nullptr) {
            const WebSafeArea page = GetWebSafeArea(GetWidth(), GetHeight());
            return Rect(static_cast<float>(page.x), static_cast<float>(page.y), static_cast<float>(page.width), static_cast<float>(page.height));
        }
#endif
        if (IsSimulated()) {
            return SimulatedSafeRect(_simulation);
        }
        SDL_Rect area{};
        if (_window == nullptr || !SDL_GetWindowSafeArea(_window, &area) || area.w <= 0 || area.h <= 0) {
            return IMainWindow::GetSafeArea();
        }
        return Rect(static_cast<float>(area.x), static_cast<float>(area.y), static_cast<float>(area.w), static_cast<float>(area.h));
    }

}  // namespace BECore
