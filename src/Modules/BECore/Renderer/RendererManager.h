#pragma once

#include <BECore/Renderer/IRenderer.h>

namespace BECore {

    class RendererManager {
    public:
        RendererManager() = default;
        ~RendererManager() = default;

        const IntrusivePtr<IRenderer>& GetRenderer() const {
            return _renderer;
        }

        void SetRenderer(IntrusivePtr<IRenderer> renderer) {
            _renderer = std::move(renderer);
        }

    private:
        IntrusivePtr<IRenderer> _renderer;
    };

}  // namespace BECore
