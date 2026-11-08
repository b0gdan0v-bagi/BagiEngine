#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/Components/SpriteFit.h>
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
        BE_REFLECT_FIELD SpriteFit _fit = SpriteFit::Stretch;

        void OnAttached() override;
        void OnDraw() override;

        /**
         * @brief Reload the texture from the sprite id or _texturePath
         *
         * Call this when _spriteId or _texturePath is changed in the editor.
         */
        void ReloadTexture();

        /**
         * @brief Draw a texture that does not come from the library or a path
         *
         * For a texture made from pixels (IRenderer::CreateTexture()). It is not serialized: ReloadTexture() drops it.
         * _srcRect still picks the part to draw; empty means the whole texture.
         */
        void SetTexture(IntrusivePtr<ITexture> texture);

        /// The texture handle (empty when the sprite has none), for SceneDrawSystem to mirror into SpriteDrawData.
        const ResourceHandle<ITexture>& GetTexture() const {
            return _texture;
        }

    private:
        ResourceHandle<ITexture> _texture;
    };

}  // namespace BECore
