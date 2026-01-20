#include "SDLMainWindow.h"

#include "BECore/Utils/ScopeGuard.h"

#include <Application/Application.h>
#include <BECore/Config/XmlConfig.h>
#include <CoreSDL/SDLUtils.h>
#include <Events/ApplicationEvents.h>
#include <Events/RenderEvents.h>

namespace BECore {

    SDLMainWindow::SDLMainWindow() = default;
    SDLMainWindow::~SDLMainWindow() = default;

    class SDLMainWindowChecker {
    public:
        // Принимаем любой вызываемый объект
        explicit SDLMainWindowChecker(std::function<void()> onExit) : onExit_(std::move(onExit)) {}

        // Деструктор сработает при выходе из {}
        ~SDLMainWindowChecker() {
            if (onExit_) {
                onExit_();
            }
        }

        // Запрещаем копирование, чтобы избежать двойного вызова
        SDLMainWindowChecker(const SDLMainWindowChecker&) = delete;
        SDLMainWindowChecker& operator=(const SDLMainWindowChecker&) = delete;

    private:
        std::function<void()> onExit_;
    };

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

            // Пробуем прочитать как строку с именами флагов
            const auto flagsStringOpt = windowNode.ParseAttribute<eastl::string_view>("windowFlags");
            if (flagsStringOpt && !flagsStringOpt->empty()) {
                flags = SDLUtils::ParseWindowFlags(*flagsStringOpt);
            }
        } else {
            return false;
        }

        if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, flags, &_window, &_renderer)) {
            return false;
        }

        _width = width;
        _height = height;

        Subscribe<RenderEvents::RenderClearEvent, &SDLMainWindow::RenderClear>(this);
        Subscribe<RenderEvents::RenderPresentEvent, &SDLMainWindow::RenderPresent>(this);
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &SDLMainWindow::Destroy>(this);
        Subscribe<RenderEvents::SetRenderDrawColorEvent, &SDLMainWindow::SetRenderDrawColor>(this);
        initialized = true;

        return true;
    }

    void SDLMainWindow::Destroy() {
        if (_window != nullptr) {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }
        if (_renderer) {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
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

    void SDLMainWindow::SetRenderDrawColor(const RenderEvents::SetRenderDrawColorEvent& event) const {
        if (_renderer) {
            SDL_SetRenderDrawColor(_renderer, event.color.r, event.color.g, event.color.b, event.color.a);
        }
    }

    void SDLMainWindow::RenderClear() const {
        if (_renderer) {
            SDL_RenderClear(_renderer);
        }
    }

    void SDLMainWindow::RenderPresent() const {
        if (_renderer) {
            SDL_RenderPresent(_renderer);
        }
    }
}  // namespace BECore
