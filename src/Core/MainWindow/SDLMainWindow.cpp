#include "SDLMainWindow.h"

#include "Core/Utils/ScopeGuard.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/Events.h>
#include <Core/MainWindow/SDLUtils.h>

namespace Core {

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

    bool SDLMainWindow::Initialize(std::string_view configPath) {

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
        SDL_WindowFlags flags;

        XmlConfig config;

        if (config.LoadFromVirtualPath(configPath)) {
            title = config.Get<std::string>("root.window.title", "My SDL3 Window");
            width = config.Get<int>("root.window.width", 800);
            height = config.Get<int>("root.window.height", 600);

            // Пробуем прочитать как строку с именами флагов
            auto flagsStringOpt = config.GetOptional<std::string>("root.window.windowFlags");
            if (flagsStringOpt && !flagsStringOpt->empty()) {
                flags = SDLUtils::ParseWindowFlags(*flagsStringOpt);
            } else {
                // Если не строка, пробуем прочитать как число (для обратной совместимости)
                flags = config.Get<SDL_WindowFlags>("root.window.windowFlags", 0);
            }
        } else {
            return false;
        }

        ;
        if (!SDL_CreateWindowAndRenderer(title.c_str(), width, height, flags, &_window, &_renderer)) {
            return false;
        }

        _width = width;
        _height = height;

        Application::GetEventManager().Subscribe<RenderClearEvent, &SDLMainWindow::RenderClear>(this);
        Application::GetEventManager().Subscribe<RenderPresentEvent, &SDLMainWindow::RenderPresent>(this);
        Application::GetEventManager().Subscribe<ApplicationCleanUpEvent, &SDLMainWindow::Destroy>(this);
        Application::GetEventManager().Subscribe<SetRenderDrawColorEvent, &SDLMainWindow::SetRenderDrawColor>(this);
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

    void SDLMainWindow::SetRenderDrawColor(const SetRenderDrawColorEvent& event) const {
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
}  // namespace Core
