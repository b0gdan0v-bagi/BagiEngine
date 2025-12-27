#pragma once
#include <Core/Utils/IntrusivePtr.h>

namespace Core {
    class IRendererHolder;

    class IMainWindow : public RefCounted {
    public:
        ~IMainWindow() override = default;

        virtual bool Create(const char* title, int width, int height, unsigned int flags = 0) = 0;
        virtual void Destroy() = 0;

        virtual bool IsValid() const = 0;

        virtual void* GetNativeWindow() const = 0;

        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;

        virtual void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) = 0;
        virtual void RenderClear() = 0;

        virtual void RenderPresent() = 0;

        virtual const IntrusivePtr<IRendererHolder>& GetRenderer() const = 0;
    };
}  // namespace Core
