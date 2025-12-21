#pragma once

#include "IMainWindow.h"
#include <SDL3/SDL.h>

/**
 * Реализация IMainWindow для SDL3
 */
class SDLMainWindow : public IMainWindow {
public:
    SDLMainWindow();
    ~SDLMainWindow() override;

    bool Create(const char* title, int width, int height, unsigned int flags = 0) override;
    void Destroy() override;
    bool IsValid() const override;
    void* GetNativeWindow() const override;
    int GetWidth() const override;
    int GetHeight() const override;

    /**
     * Получает прямой указатель на SDL_Window (для совместимости)
     * @return Указатель на SDL_Window или nullptr
     */
    SDL_Window* GetSDLWindow() const { return _window; }

    bool CreateRenderer() override;

    void DestroyRenderer() override;

    SDL_Renderer* GetRenderer() const { return _renderer; }

    void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) override;
    void RenderClear() override;
    void RenderPresent() override;

private:
    SDL_Window* _window = nullptr;
    SDL_Renderer* _renderer = nullptr;
    int _width = 0;
    int _height = 0;
};

