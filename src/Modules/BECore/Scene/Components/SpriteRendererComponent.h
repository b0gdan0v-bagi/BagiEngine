#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    namespace SceneEvents {
        struct SceneDrawEvent;
    }

    class SpriteRendererComponent : public IComponent {
        BE_CLASS(SpriteRendererComponent)
    public:
        SpriteRendererComponent() = default;
        ~SpriteRendererComponent() override = default;

        BE_REFLECT_FIELD PoolString _spriteId;
        BE_REFLECT_FIELD PoolString _texturePath;
        BE_REFLECT_FIELD Rect _srcRect{};

        void OnAttached() override;

        /**
         * @brief Reload texture from _texturePath and re-subscribe to draw events
         *
         * Call this when _texturePath is changed in the editor.
         */
        void ReloadTexture();

    private:
        void OnDraw(const SceneEvents::SceneDrawEvent&);

        ResourceHandle<ITexture> _texture;
    };

}  // namespace BECore
