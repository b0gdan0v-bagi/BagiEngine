#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    class SpriteRendererComponent : public IComponent {
        BE_CLASS(SpriteRendererComponent)
    public:
        SpriteRendererComponent() = default;
        ~SpriteRendererComponent() override = default;

        BE_REFLECT_FIELD PoolString _spriteId;
        BE_REFLECT_FIELD PoolString _texturePath;
        BE_REFLECT_FIELD Rect _srcRect{};

        void OnAttached() override;
        void OnDraw() override;

        /**
         * @brief Reload the texture from the sprite id or _texturePath
         *
         * Call this when _spriteId or _texturePath is changed in the editor.
         */
        void ReloadTexture();

    private:
        ResourceHandle<ITexture> _texture;
    };

}  // namespace BECore
