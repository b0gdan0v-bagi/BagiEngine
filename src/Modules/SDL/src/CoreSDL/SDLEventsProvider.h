#pragma once

#include <BECore/Input/FingerPointers.h>
#include <Events/IEventsProvider.h>
#include <SDL3/SDL.h>

namespace BECore {

    class SDLEventsProvider : public IEventsProvider {
    public:
        SDLEventsProvider() = default;
        ~SDLEventsProvider() override = default;

        bool Initialize() override;
        void Destroy() override;
        void ProcessEvents() override;

    private:
        bool _isInitialized = false;

        void ConvertSDLEvent(const SDL_Event& sdlEvent);
        void ConvertFingerEvent(const SDL_TouchFingerEvent& finger, SDL_EventType type);

        FingerPointers _fingers;
    };

}  // namespace BECore
