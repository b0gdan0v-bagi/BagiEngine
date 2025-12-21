#pragma once

#include <SDL3/SDL.h>

class SDLMainWindow;

/**
 * Менеджер для управления ImGui
 */
class ImGuiManager {
   public:
    ImGuiManager() = default;
    ~ImGuiManager();

    /**
     * Инициализация ImGui
     * @param window SDL окно
     * @param renderer SDL рендерер
     * @return true если инициализация успешна
     */
    bool Initialize(SDL_Window* window, SDL_Renderer* renderer);

    /**
     * Завершение работы ImGui
     */
    void Shutdown();

    /**
     * Начало нового кадра ImGui (вызывать в начале цикла рендеринга)
     */
    void NewFrame();

    /**
     * Обработка событий SDL для ImGui
     * @param event SDL событие
     */
    void ProcessEvent(const SDL_Event* event);

    /**
     * Рендеринг ImGui (вызывать после отрисовки UI)
     */
    void Render();

    /**
     * Проверка, инициализирован ли ImGui
     */
    bool IsInitialized() const { return _isInitialized; }

private:
    bool _isInitialized = false;
    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
};

