#pragma once

#include <Core/Events/IEventsProvider.h>
#include <Core/Events/EventManager.h>
#include <SDL3/SDL.h>

namespace Core {

    class SDLEventsProvider : public IEventsProvider {
    public:
        SDLEventsProvider() = default;
        ~SDLEventsProvider() override = default;

        bool Initialize() override;
        void Destroy() override;
        void ProcessEvents() override;

    private:
        bool _isInitialized = false;

        // Преобразование SDL события в EnTT событие
        void ConvertSDLEvent(const SDL_Event& sdlEvent);
    };

} // namespace Core

