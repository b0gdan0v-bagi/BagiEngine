#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

int main(int argc, char* argv[]) {
    // 1. Инициализация
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return -1;
    }

    // 2. Создание окна и рендерера (в SDL3 это можно сделать одной функцией)
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;

    if (!SDL_CreateWindowAndRenderer("My SDL3 Window", 800, 600, 0, &window, &renderer)) {
        SDL_Quit();
        return -1;
    }

    // 3. Главный цикл
    bool quit = false;
    SDL_Event event;

    while (!quit) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                quit = true;
            }
        }

        // Заливка экрана цветом (RGBC — Красный, Зеленый, Синий, Альфа)
        SDL_SetRenderDrawColor(renderer, 20, 20, 100, 255); // Темно-синий
        SDL_RenderClear(renderer);

        // Показываем результат
        SDL_RenderPresent(renderer);
    }

    // 4. Завершение
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}