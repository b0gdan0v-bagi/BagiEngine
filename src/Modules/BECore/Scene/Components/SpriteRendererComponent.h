#pragma once

#include <BECore/Memory/Pooled.h>
#include <BECore/Renderer/BlendMode.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/Components/SpriteFit.h>
#include <BECore/Scene/IComponent.h>

namespace BECore {

    class SpriteRendererComponent : public IComponent {
        BE_CLASS(SpriteRendererComponent, ICON = image)
    public:
        BE_POOLED_SINGLE_THREAD(SpriteRendererComponent)

        SpriteRendererComponent() = default;
        ~SpriteRendererComponent() override = default;
        bool HasOnDraw() const override {
            return false;
        }

        BE_REFLECT_FIELD PoolString _spriteId;
        BE_REFLECT_FIELD BE_FIELD_ATTRS(asset(Texture)) PoolString _texturePath;
        BE_REFLECT_FIELD Rect _srcRect{};
        BE_REFLECT_FIELD SpriteFit _fit = SpriteFit::Stretch;
        BE_REFLECT_FIELD BlendMode _blend = BlendMode::Alpha;  ///< how the sprite blends with what is behind it; Alpha is straight alpha
        BE_REFLECT_FIELD bool _flipX = false;                  ///< mirrors the picture left to right about its own center
        BE_REFLECT_FIELD bool _flipY = false;                  ///< mirrors the picture top to bottom

        void OnAttached() override;
        void OnPropertyWritten(StringView field) override;

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

        /// The texture handle (empty when the sprite has none), read by SceneDrawSystem at draw time.
        const ResourceHandle<ITexture>& GetTexture() const {
            return _texture;
        }

    private:
        ResourceHandle<ITexture> _texture;
    };

}  // namespace BECore
