#include "SDLMainWindow.h"

#include <Application/Application.h>
#include <Core/Config/XmlConfig.h>
#include <Core/Events/Events.h>
#include <Core/ImGui/SDL3imGuiManager.h>
#include <Core/MainWindow/SDLRendererHolder.h>
#include <Core/MainWindow/SDLUtils.h>

namespace Core {

    SDLMainWindow::SDLMainWindow() = default;

    SDLMainWindow::~SDLMainWindow() = default;

    bool SDLMainWindow::Initialize(std::string_view configPath) {

        if (!SDL_Init(SDL_INIT_VIDEO)) {
            return false;
        }

        if (_window != nullptr) {
            // Окно уже создано
            return false;
        }

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

        _window = SDL_CreateWindow(title.c_str(), width, height, flags);
        if (_window == nullptr) {
            return false;
        }

        auto rendererHolder = Core::New<SDLRendererHolder>();
        if (!rendererHolder) {
            return false;
        }
        rendererHolder->Create(*this);
        _renderer = rendererHolder;

        _width = width;
        _height = height;

        Application::GetInstance().GetEventManager().Subscribe<RenderClearEvent, &SDLMainWindow::RenderClear>(this);
        Application::GetInstance().GetEventManager().Subscribe<RenderPresentEvent, &SDLMainWindow::RenderPresent>(this);

        return true;
    }

    void SDLMainWindow::Destroy() {
        if (_window != nullptr) {
            SDL_DestroyWindow(_window);
            _window = nullptr;
        }
        if (_renderer) {
            _renderer->Destroy();
        }
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

    void SDLMainWindow::SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) {
        if (_renderer) {
            _renderer->SetRenderDrawColor(r, g, b, a);
        }
    }

    void SDLMainWindow::RenderClear() const {
        if (_renderer) {
            _renderer->RenderClear();
        }
    }

    void SDLMainWindow::RenderPresent() const {
        if (_renderer) {
            _renderer->RenderPresent();
        }
    }
}  // namespace Core
