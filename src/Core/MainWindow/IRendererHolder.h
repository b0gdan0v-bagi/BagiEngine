#pragma once
#include <Core/Utils/IntrusivePtr.h>

namespace Core {
    class IMainWindow;

    class IRendererHolder : public RefCounted {
       public:
        ~IRendererHolder() override = default;

        virtual bool Create(const IMainWindow& window) = 0;
        virtual void Destroy() = 0;

        virtual void SetRenderDrawColor(unsigned char r, unsigned char g, unsigned char b, unsigned char a) = 0;
        virtual void RenderClear() = 0;
        virtual void RenderPresent() = 0;
    };
}  // namespace Core
