#pragma once

namespace Core {
    class EventManager;

    class IimGuiManager : public RefCounted {
    public:
        IimGuiManager() = default;
        ~IimGuiManager() override = default;

        virtual bool Initialize() = 0;
        virtual void Destroy() = 0;

        virtual void NewFrame() = 0;
        virtual void Render() = 0;

        virtual bool IsInitialized() const = 0;
    };
}  // namespace Core
